#!/usr/bin/env python3

import sys
import os
import json
import subprocess
from collections import defaultdict
from systrace_pb2 import ProcMem, MemAllocEntry, StackFrame, StageType

class SymbolResolver:
    def __init__(self):
        self._symbol_cache = {}
        self._path_cache = {}

    def resolve_frame(self, frame):
        if not frame.so_name or not frame.address:
            return f"unknown@{hex(frame.address)}"

        abs_path = self._resolve_path(frame.so_name)
        cache_key = (abs_path, frame.address)
        
        if cache_key in self._symbol_cache:
            return self._symbol_cache[cache_key]

        resolved = self._resolve_symbol(abs_path, frame.address)
        self._symbol_cache[cache_key] = resolved
        return resolved

    def _resolve_path(self, rel_path):
        if rel_path.startswith('/'):
            return rel_path

        if rel_path in self._path_cache:
            return self._path_cache[rel_path]

        search_paths = [
            '/lib', '/lib64', 
            '/usr/lib', '/usr/lib64',
            '/usr/local/lib', 
            os.getcwd()
        ]

        for path in search_paths:
            abs_path = os.path.join(path, rel_path)
            if os.path.exists(abs_path):
                self._path_cache[rel_path] = abs_path
                return abs_path

        return rel_path

    def _resolve_symbol(self, abs_path, address):
        if not os.path.exists(abs_path):
            return f"unresolved@{hex(address)}"

        try:
            cmd = [
                "addr2line",
                "-e", abs_path,
                "-f", "-p",
                hex(address)
            ]
            result = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=1.0
            )
            if result.returncode == 0:
                func = result.stdout.split(' ')[0].strip()
                return f"{func}@{hex(address)}" if func else f"unresolved@{hex(address)}"
        except Exception as e:
            print(f"[WARNING] Symbol resolve failed for {abs_path}:{hex(address)} - {str(e)}")
        
        return f"unresolved@{hex(address)}"

class ProcMemConverter:
    def __init__(self):
        self.stage_names = {
            StageType.STAGE_DATALOADER: "DATA_LOAD",
            StageType.STAGE_FORWARD: "FORWARD",
            StageType.STAGE_BACKWARD: "BACKWARD"
        }
        self.symbol_resolver = SymbolResolver()

    def convert(self, input_pb, output_json):
        print(f"[INFO] Converting {input_pb} -> {output_json}")
        proc_mem = self._load_proc_mem(input_pb)
        card_data = self._analyze_allocations(proc_mem)
        events = self._generate_flamegraph_events(card_data)
        self._save_json(output_json, events)
        print(f"[SUCCESS] Saved {len(events)} events to {output_json}")

    def _load_proc_mem(self, path):
        print(f"[DEBUG] Loading {path}")
        with open(path, "rb") as f:
            proc_mem = ProcMem()
            proc_mem.ParseFromString(f.read())
            print(f"[DEBUG] Loaded {len(proc_mem.mem_alloc_stacks)} allocs, "
                  f"{len(proc_mem.mem_free_stacks)} frees")
            return proc_mem

    def _analyze_allocations(self, proc_mem):
        freed_ptrs = {free.alloc_ptr for free in proc_mem.mem_free_stacks}
        
        active_allocs = defaultdict(list)
        for alloc in proc_mem.mem_alloc_stacks:
            if alloc.alloc_ptr not in freed_ptrs:
                key = (alloc.stage_type, alloc.stage_id)
                active_allocs[key].append(alloc)
        
        print(f"[DEBUG] Found {len(active_allocs)} active allocation groups")
        return {
            "pid": proc_mem.pid,
            "alloc_groups": active_allocs,
            "total_allocs": len(proc_mem.mem_alloc_stacks),
            "total_frees": len(freed_ptrs)
        }

    def _build_call_tree(self, allocations, stage_type):
        root = {
            "name": self.stage_names.get(stage_type, "UNKNOWN"),
            "children": {},
            "size": 0
        }
        
        for alloc in allocations:
            current = root
            path = [self.symbol_resolver.resolve_frame(frame) for frame in alloc.stack_frames]
            
            for frame in path:
                if frame not in current["children"]:
                    current["children"][frame] = {
                        "name": frame,
                        "children": {},
                        "size": 0
                    }
                current = current["children"][frame]
            current["size"] += alloc.mem_size
        
        self._compute_tree_sizes(root)
        return root

    def _compute_tree_sizes(self, node):
        if not node["children"]:
            return node["size"]
        
        total = 0
        for child in node["children"].values():
            total += self._compute_tree_sizes(child)
        
        node["size"] = total
        return total

    def _generate_flamegraph_events(self, card_data):
        print(f"[DEBUG] Generating events for PID {card_data['pid']}")
        events = []
        current_time = 0

        sorted_groups = sorted(
            card_data["alloc_groups"].items(),
            key=lambda x: x[0][1]
        )
        
        for (stage_type, stage_id), allocs in sorted_groups:
            call_tree = self._build_call_tree(allocs, stage_type)
            events.extend(
                self._create_events_from_tree(
                    call_tree,
                    card_data["pid"],
                    current_time,
                    stage_type,
                    stage_id
                )
            )
            current_time += call_tree["size"]
        
        return events

    def _create_events_from_tree(self, tree, pid, start_time, stage_type, stage_id):
        events = []
        
        def _traverse(node, ts, depth):
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
            
            for child in sorted(node["children"].values(), key=lambda x: x["name"]):
                _traverse(child, ts, depth + 1)
                ts += child["size"]
        
        _traverse(tree, start_time, 0)
        return events

    def _save_json(self, path, events):
        output = {
            "traceEvents": sorted(events, key=lambda x: x["ts"]),
            "displayTimeUnit": "ns",
            "metadata": {
                "description": "Memory Allocation Flamegraph",
                "source": "ProcMem Converter"
            }
        }
        
        with open(path, "w") as f:
            json.dump(output, f, indent=2)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python proc_mem_converter.py input.pb output.json")
        sys.exit(1)
    
    converter = ProcMemConverter()
    converter.convert(sys.argv[1], sys.argv[2])