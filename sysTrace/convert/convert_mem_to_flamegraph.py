#!/usr/bin/env python3

import sys
import json
import os
import subprocess
from collections import defaultdict
from systrace_pb2 import ProcMem, StageType

class ProcMemConverter:
    def __init__(self):
        self.stage_names = {
            StageType.STAGE_DATALOADER: "DATA_LOAD",
            StageType.STAGE_FORWARD: "FORWARD",
            StageType.STAGE_BACKWARD: "BACKWARD"
        }
        self.symbol_cache = {}
        self.so_path_cache = {}

    def convert(self, input_pb, output_json):
        proc_mem = self._load_proc_mem(input_pb)
        card_data = self._analyze_allocations(proc_mem)
        events = self._generate_flamegraph_events(card_data)
        self._save_json(output_json, events)

    def _load_proc_mem(self, path):
        with open(path, "rb") as f:
            proc_mem = ProcMem()
            proc_mem.ParseFromString(f.read())
            return proc_mem

    def _analyze_allocations(self, proc_mem):
        freed_ptrs = {free.alloc_ptr for free in proc_mem.mem_free_stacks}
        active_allocs = defaultdict(list)
        
        for alloc in proc_mem.mem_alloc_stacks:
            if alloc.alloc_ptr not in freed_ptrs:
                active_allocs[(alloc.stage_type, alloc.stage_id)].append(alloc)
        
        return {"pid": proc_mem.pid, "alloc_groups": active_allocs}

    def _generate_flamegraph_events(self, card_data):
        events = []
        current_time = 0
        
        for (stage_type, stage_id), allocs in sorted(
            card_data["alloc_groups"].items(), 
            key=lambda x: x[0][1]
        ):
            call_tree = self._build_call_tree(allocs, stage_type)
            events.extend(self._create_events_from_tree(
                call_tree, card_data["pid"], current_time, stage_type, stage_id
            ))
            current_time += call_tree["size"]
        
        return events

    def _resolve_symbol(self, so_name, address):
        cache_key = f"{so_name}:{address}"
        if cache_key in self.symbol_cache:
            return self.symbol_cache[cache_key]
        
        so_path = self._find_so_path(so_name)
        if not so_path:
            return f"{so_name}@0x{address:x}"
        
        try:
            result = subprocess.run(
                ["addr2line", "-e", so_path, "-f", "-C", "-p", f"0x{address:x}"],
                capture_output=True, text=True
            )
            func_name = result.stdout.split(" at ")[0].split("(")[0] if result.returncode == 0 else ""
            symbol = f"{so_name}@{func_name}" if func_name else f"{so_name}@0x{address:x}"
            self.symbol_cache[cache_key] = symbol
            return symbol
        except:
            return f"{so_name}@0x{address:x}"

    def _find_so_path(self, so_name):
        if so_name in self.so_path_cache:
            return self.so_path_cache[so_name]
        
        if os.path.isabs(so_name) and os.path.exists(so_name):
            return so_name
        
        base_name = os.path.basename(so_name)
        search_paths = {
            *os.getenv("LD_LIBRARY_PATH", "").split(":"),
            *os.getenv("PATH", "").split(":"),
            "/usr/lib", "/usr/local/lib", "/lib", 
            "/usr/lib/x86_64-linux-gnu",
            "/usr/lib/aarch-linux-gnu",
            "/usr/local/Ascend/ascend-toolkit/latest",
            os.path.expanduser("~/.local/lib")
        }
        
        for path in filter(os.path.isdir, search_paths):
            for root, _, files in os.walk(path):
                if base_name in files:
                    full_path = os.path.join(root, base_name)
                    self.so_path_cache[so_name] = full_path
                    return full_path
                
                if base_name.startswith("lib") and ".so" in base_name:
                    lib_prefix = base_name.split(".so")[0]
                    for file in files:
                        if file.startswith(lib_prefix) and ".so" in file:
                            full_path = os.path.join(root, file)
                            self.so_path_cache[so_name] = full_path
                            return full_path
        
        return None

    def _build_call_tree(self, allocations, stage_type):
        root = {"name": self.stage_names.get(stage_type, "UNKNOWN"), "children": {}, "size": 0}
        
        for alloc in allocations:
            current = root
            for frame in alloc.stack_frames:
                so_name = os.path.basename(frame.so_name)
                symbol = self._resolve_symbol(so_name, frame.address)
                
                if symbol not in current["children"]:
                    current["children"][symbol] = {"name": symbol, "children": {}, "size": 0}
                current = current["children"][symbol]
            
            current["size"] += alloc.mem_size
        
        self._compute_tree_sizes(root)
        return root

    def _compute_tree_sizes(self, node):
        node["size"] = sum(
            self._compute_tree_sizes(child) 
            for child in node["children"].values()
        ) if node["children"] else node["size"]
        return node["size"]

    def _create_events_from_tree(self, tree, pid, start_time, stage_type, stage_id):
        events = []
        
        def traverse(node, ts, depth):
            events.append({
                "name": node["name"],
                "ph": "X",
                "ts": ts,
                "dur": node["size"],
                "pid": pid,
                "tid": pid,
                "args": {
                    "depth": depth,
                    "stage_type": stage_type,
                    "stage_id": stage_id,
                    "bytes": node["size"]
                }
            })
            
            child_start = ts
            for child in sorted(node["children"].values(), key=lambda x: x["name"]):
                traverse(child, child_start, depth + 1)
                child_start += child["size"]
        
        traverse(tree, start_time, 0)
        return events

    def _save_json(self, path, events):
        with open(path, "w") as f:
            json.dump({
                "traceEvents": sorted(events, key=lambda x: x["ts"]),
                "displayTimeUnit": "ns",
                "metadata": {
                    "description": "Memory Allocation Flamegraph",
                    "source": "ProcMem Converter"
                }
            }, f, indent=2)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python proc_mem_converter.py input.pb output.json")
        sys.exit(1)
    
    ProcMemConverter().convert(sys.argv[1], sys.argv[2])