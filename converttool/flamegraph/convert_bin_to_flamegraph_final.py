#!/usr/bin/env python3

import sys
import json
from collections import defaultdict
from mem_profile_pb2 import Mem, ProcMem, MemAllocEntry, MemFreeEntry, StackFrame, StageType

class FlameGraphConverter:
    def __init__(self):
        self.stage_categories = {
            StageType.STAGE_DATALOADER: "DATALOADER",
            StageType.STAGE_FORWARD: "FORWARD",
            StageType.STAGE_BACKWARD: "BACKWARD"
        }

    def convert(self, input_path, output_path):
        print(f"[DEBUG] Starting conversion: {input_path} -> {output_path}")
        try:
            mem_data = self._load_proto(input_path)
            print(f"[DEBUG] Successfully loaded proto data, {len(mem_data.proc_mem)} process(es) found")
            card_allocations = self._group_by_card(mem_data)
            print(f"[DEBUG] Grouped by card, {len(card_allocations)} card(s) found")
            events = self._generate_events(card_allocations)
            print(f"[DEBUG] Generated {len(events)} events")
            self._save_output(output_path, events)
        except Exception as e:
            print(f"[ERROR] Conversion failed at step: {str(e)}", file=sys.stderr)
            raise

    def _load_proto(self, path):
        print(f"[DEBUG] Loading proto file from {path}")
        try:
            with open(path, "rb") as f:
                data = f.read()
                print(f"[DEBUG] Read {len(data)} bytes from file")
                mem_data = Mem.FromString(data)
                
                # 添加详细检查
                print(f"[DEBUG] Proto fields: {mem_data.ListFields()}")
                if not mem_data.ListFields():
                    print("[WARNING] Proto file contains no data fields!")
                
                print(f"[DEBUG] Proto parsed successfully")
                return mem_data
        except Exception as e:
            print(f"[ERROR] Failed to load proto file: {str(e)}", file=sys.stderr)
            raise

    def _group_by_card(self, mem_data):
        print(f"[DEBUG] Grouping memory data by card")
        card_data = defaultdict(lambda: defaultdict(list))
        total_allocs = 0
        total_frees = 0
        
        for proc_idx, proc_mem in enumerate(mem_data.proc_mem):
            print(f"[DEBUG] Processing process {proc_idx} (pid={proc_mem.pid})")
            card_id = proc_mem.pid
            freed_ptrs = {f.alloc_ptr for f in proc_mem.mem_free_stacks}
            total_frees += len(freed_ptrs)
            
            alloc_count = 0
            for alloc in proc_mem.mem_alloc_stacks:
                if alloc.alloc_ptr not in freed_ptrs:
                    card_data[card_id][(alloc.stage_type, alloc.stage_id)].append(alloc)
                    alloc_count += 1
            total_allocs += alloc_count
            print(f"[DEBUG] Process {proc_idx}: {alloc_count} active allocations, {len(freed_ptrs)} frees")
        
        print(f"[DEBUG] Total allocations: {total_allocs}, total frees: {total_frees}")
        return card_data

    def _generate_events(self, card_allocations):
        print(f"[DEBUG] Generating events from card allocations")
        all_events = []

        for card_id, stage_groups in card_allocations.items():
            print(f"[DEBUG] Processing card {card_id} with {len(stage_groups)} stage groups")
            sorted_groups = sorted(
                stage_groups.items(),
                key=lambda x: x[0][1]  # Sort by stage_id
            )

            current_pos = 0
            for (stage_type, stage_id), allocs in sorted_groups:
                print(f"[DEBUG] Card {card_id} stage {stage_id} ({stage_type}): {len(allocs)} allocations")
                tree = self._build_call_tree(allocs, stage_type, stage_id)
                print(f"[DEBUG] Built call tree with total size {tree['mem_size']} bytes")
                events = self._tree_to_events(tree, card_id, current_pos, stage_type, stage_id)
                all_events.extend(events)
                current_pos += tree["mem_size"]

        return all_events

    def _build_call_tree(self, allocations, stage_type, stage_id):
        print(f"[DEBUG] Building call tree for {len(allocations)} allocations")
        root = {
            "name": self.stage_categories.get(stage_type, "UNKNOWN"),
            "children": {},
            "mem_size": 0
        }

        path_counts = defaultdict(int)
        for alloc in allocations:
            path_key = tuple(f"{f.so_name}@{f.address}" for f in alloc.stack_frames)
            path_counts[path_key] += alloc.mem_size

        print(f"[DEBUG] Found {len(path_counts)} unique call paths")
        
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
        total = 0
        for child in node["children"].values():
            total += self._compute_tree_sizes(child)
        node["mem_size"] = total
        return total

    def _tree_to_events(self, tree, card_id, start_time, stage_type, stage_id):
        print(f"[DEBUG] Converting tree to events for card {card_id}, starting at {start_time}")
        events = []

        def traverse(node, current_time, depth):
            events.append({
                "name": node["name"],
                "cat": self.stage_categories.get(stage_type, "UNKNOWN"),
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
        print(f"[DEBUG] Generated {len(events)} events from tree")
        return events

    def _save_output(self, path, events):
        print(f"[DEBUG] Saving output to {path}")
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

        print(f"[INFO] Converted successfully! Saved to {path}")

def main():
    if len(sys.argv) != 3:
        print("Usage: python convert_bin_to_flamegraph.py <input.bin> <output.json>")
        sys.exit(1)

    try:
        converter = FlameGraphConverter()
        converter.convert(sys.argv[1], sys.argv[2])
    except Exception as e:
        print(f"[FATAL] Error: {str(e)}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
