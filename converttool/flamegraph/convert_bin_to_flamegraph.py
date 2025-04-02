#!/usr/bin/env python3

import sys
import json
import os
from collections import defaultdict
from mem_profile_pb2 import Mem

class FlameGraphConverter:
    def __init__(self):
        self.stage_categories = {
            0: "DATALOADER",
            1: "FORWARD",
            2: "BACKWARD"
        }

    def convert(self, input_path, output_path):
        mem_data = self._load_proto(input_path)
        card_allocations = self._group_by_card(mem_data)
        events = self._generate_events(card_allocations)
        self._save_output(output_path, events)

    def _load_proto(self, path):
        with open(path, "rb") as f:
            return Mem.FromString(f.read())

    def _group_by_card(self, mem_data):
        card_data = defaultdict(lambda: defaultdict(list))
        for proc_mem in mem_data.proc_mem:
            card_id = proc_mem.pid
            freed_ptrs = {f.alloc_ptr for f in proc_mem.mem_free_stacks}
            for alloc in proc_mem.mem_alloc_stacks:
                if alloc.alloc_ptr not in freed_ptrs:
                    card_data[card_id][(alloc.stage_type, alloc.stage_id)].append(alloc)
        return card_data

    def _generate_events(self, card_allocations):
        all_events = []
        
        for card_id, stage_groups in card_allocations.items():
            sorted_groups = sorted(
                stage_groups.items(),
                key=lambda x: x[0][1]  # Sort by stage_id
            )
            
            current_pos = 0
            for (stage_type, stage_id), allocs in sorted_groups:
                tree = self._build_call_tree(allocs, stage_type, stage_id)
                events = self._tree_to_events(tree, card_id, current_pos, stage_type, stage_id)
                all_events.extend(events)
                current_pos += tree["mem_size"]
                
        return all_events

    def _build_call_tree(self, allocations, stage_type, stage_id):
        root = {
            #"name": f"{stage_type} [id={stage_id}]",
            "name": stage_type,
            "children": {},
            "mem_size": 0
        }
        
        path_counts = defaultdict(int)
        for alloc in allocations:
            path_key = tuple(f"{f.so_name}@{f.address}" for f in alloc.stack_frames)
            path_counts[path_key] += alloc.mem_size
            
        for path, size in path_counts.items():
            current = root
            for frame in path:
                if frame not in current["children"]:
                    current["children"][frame] = {
                        "name": frame,
                        "children": {},
                        "mem_size": 0
                    }
                current = current["children"][frame]
            current["mem_size"] += size
            
        self._compute_tree_sizes(root)
        return root

    def _compute_tree_sizes(self, node):
        if not node["children"]:
            return node["mem_size"]
        total = sum(self._compute_tree_sizes(child) for child in node["children"].values())
        node["mem_size"] = total
        return total

    def _tree_to_events(self, tree, card_id, start_time, stage_type, stage_id):
        events = []
        
        def traverse(node, current_time, depth):
            events.append({
                "name": node["name"],
                "cat": self.stage_categories.get(stage_id, "UNKNOWN"),
                "ph": "X",
                "ts": current_time,
                "dur": node["mem_size"],
                "pid": card_id,
                "tid": card_id,
                "args": {
                    "depth": depth,
                    "mem_bytes": node["mem_size"],
                    "stage_type": stage_type,
                    "stage_id": stage_id
                }
            })
            
            child_start = current_time
            for child in sorted(node["children"].values(), key=lambda x: x["name"]):
                traverse(child, child_start, depth + 1)
                child_start += child["mem_size"]
                
        traverse(tree, start_time, 0)
        return events

    def _save_output(self, path, events):
        result = {
            "traceEvents": sorted(events, key=lambda x: (x["pid"], x["ts"])),
            "displayTimeUnit": "ns",
            "metadata": {
                "description": "Memory FlameGraph (Sorted by stage_id)",
                "card_count": len(set(e["pid"] for e in events))
            }
        }
        
        with open(path, "w") as f:
            json.dump(result, f, indent=2)
            
        print(f"convert successfully!")

def main():
    if len(sys.argv) != 3:
        print("Usage: python convert_bin_to_flamegraph.py <input.bin> <output.json>")
        sys.exit(1)
        
    try:
        converter = FlameGraphConverter()
        converter.convert(sys.argv[1], sys.argv[2])
    except Exception as e:
        print(f"error: {str(e)}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
