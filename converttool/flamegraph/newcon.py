#!/usr/bin/env python3
"""
mem_to_flamegraph_final.py - 多阶段水平排列的内存火焰图转换工具
特性：
1. pid/tid固定为卡号，所有阶段水平排列
2. 严格保持调用栈父子关系
3. 自动合并相同调用路径的内存分配
4. 支持任意深度的调用栈
"""

import sys
import json
import os
from collections import defaultdict
from mem_profile_pb2 import Mem

class FlameGraphConverter:
    def __init__(self):
        self.stage_names = {
            0: "STAGE_DATALOADER",
            1: "STAGE_FORWARD",
            2: "STAGE_BACKWARD"
        }

    def convert(self, input_path, output_path):
        """主转换流程"""
        mem_data = self._load_proto(input_path)
        card_allocations = self._group_by_card(mem_data)
        events = self._generate_events(card_allocations)
        self._save_output(output_path, events)

    def _load_proto(self, path):
        """加载protobuf数据"""
        if not os.path.exists(path):
            raise FileNotFoundError(f"Input file not found: {path}")
        
        with open(path, "rb") as f:
            return Mem.FromString(f.read())

    def _group_by_card(self, mem_data):
        """按卡号分组内存分配"""
        card_data = defaultdict(lambda: defaultdict(list))
        
        for proc_mem in mem_data.proc_mem:
            card_id = proc_mem.pid  # pid/tid都使用卡号
            freed_ptrs = {f.alloc_ptr for f in proc_mem.mem_free_stacks}
            
            for alloc in proc_mem.mem_alloc_stacks:
                if alloc.alloc_ptr not in freed_ptrs:
                    card_data[card_id][alloc.stage_id].append(alloc)
        
        return card_data

    def _generate_events(self, card_allocations):
        """生成水平排列的事件"""
        all_events = []
        global_start = 0  # 全局起始位置
        
        for card_id, stages in card_allocations.items():
            # 为每个阶段构建独立的调用树
            stage_trees = {}
            for stage_id, allocs in stages.items():
                stage_trees[stage_id] = self._build_call_tree(allocs)
            
            # 水平排列各阶段
            current_pos = global_start
            for stage_id in sorted(stage_trees.keys()):
                tree = stage_trees[stage_id]
                events = self._tree_to_events(tree, card_id, current_pos)
                all_events.extend(events)
                current_pos += tree["mem_size"]  # 下一个阶段紧接排列
            
            global_start = current_pos  # 下一张卡从新位置开始
        
        return all_events

    def _build_call_tree(self, allocations):
        """构建调用树结构"""
        root = {
            "name": self.stage_names[allocations[0].stage_id],
            "children": {},
            "mem_size": 0
        }

        # 合并相同调用路径的分配
        path_counts = defaultdict(int)
        for alloc in allocations:
            path_key = tuple(f"{f.so_name}@{f.address}" for f in alloc.stack_frames)
            path_counts[path_key] += alloc.mem_size

        # 构建树结构
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

        # 计算父节点内存
        self._compute_tree_sizes(root)
        return root

    def _compute_tree_sizes(self, node):
        """后序遍历计算内存大小"""
        if not node["children"]:
            return node["mem_size"]
        
        total = 0
        for child in node["children"].values():
            total += self._compute_tree_sizes(child)
        
        node["mem_size"] = total  # 父节点内存等于子节点之和
        return total

    def _tree_to_events(self, tree, card_id, start_time):
        """将树转换为事件列表"""
        events = []
        
        def traverse(node, current_time, depth):
            events.append({
                "name": node["name"],
                "cat": node["name"].split("_")[-1],  # 提取阶段类型
                "ph": "X",
                "ts": current_time,
                "dur": node["mem_size"],
                "pid": card_id,
                "tid": card_id,  # 与pid相同
                "args": {
                    "depth": depth,
                    "mem_bytes": node["mem_size"]
                }
            })

            # 子节点紧贴排列
            child_start = current_time
            for child in sorted(node["children"].values(), key=lambda x: x["name"]):
                traverse(child, child_start, depth + 1)
                child_start += child["mem_size"]

        traverse(tree, start_time, 0)
        return events

    def _save_output(self, path, events):
        """保存结果文件"""
        result = {
            "traceEvents": sorted(events, key=lambda x: (x["pid"], x["ts"])),
            "displayTimeUnit": "ns",
            "metadata": {
                "description": "Memory FlameGraph (Horizontal Stages)",
                "card_count": len(set(e["pid"] for e in events))
            }
        }
        
        with open(path, "w") as f:
            json.dump(result, f, indent=2)
        
        print(f"Successfully generated {len(events)} events")

def main():
    if len(sys.argv) != 3:
        print("Usage: python mem_to_flamegraph_final.py <input.bin> <output.json>")
        sys.exit(1)
    
    try:
        converter = FlameGraphConverter()
        converter.convert(sys.argv[1], sys.argv[2])
    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
