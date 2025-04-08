#!/usr/bin/env python3
"""
proc_mem_converter.py - 专为解析裸 ProcMem 消息设计的转换脚本
（兼容C程序生成的原始二进制格式）
"""

import sys
import json
from collections import defaultdict
from mem_profile_pb2 import ProcMem, MemAllocEntry, StackFrame, StageType

class ProcMemConverter:
    def __init__(self):
        # 阶段类型映射
        self.stage_names = {
            StageType.STAGE_DATALOADER: "DATA_LOAD",
            StageType.STAGE_FORWARD: "FORWARD",
            StageType.STAGE_BACKWARD: "BACKWARD"
        }

    def convert(self, input_pb, output_json):
        """主转换流程"""
        print(f"[INFO] Converting {input_pb} -> {output_json}")
        
        # 1. 加载并解析二进制文件
        proc_mem = self._load_proc_mem(input_pb)
        
        # 2. 分析内存分配数据
        card_data = self._analyze_allocations(proc_mem)
        
        # 3. 生成火焰图事件
        events = self._generate_flamegraph_events(card_data)
        
        # 4. 保存结果
        self._save_json(output_json, events)
        print(f"[SUCCESS] Saved {len(events)} events to {output_json}")

    def _load_proc_mem(self, path):
        """直接解析 ProcMem 消息（不包含 Mem 外层）"""
        print(f"[DEBUG] Loading raw ProcMem from {path}")
        with open(path, "rb") as f:
            data = f.read()
            print(f"[DEBUG] Read {len(data)} bytes")
            
            proc_mem = ProcMem()
            proc_mem.ParseFromString(data)
            
            # 验证数据完整性
            if not proc_mem.pid:
                print("[WARNING] PID field is empty!")
            print(f"[DEBUG] Found {len(proc_mem.mem_alloc_stacks)} allocs, "
                  f"{len(proc_mem.mem_free_stacks)} frees")
            
            return proc_mem

    def _analyze_allocations(self, proc_mem):
        """按卡和阶段分组内存分配"""
        print(f"[DEBUG] Analyzing allocations for PID {proc_mem.pid}")
        
        # 获取已释放的指针集合
        freed_ptrs = {free.alloc_ptr for free in proc_mem.mem_free_stacks}
        
        # 按 (stage_type, stage_id) 分组未释放的分配
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

    def _generate_flamegraph_events(self, card_data):
        """生成Chrome火焰图格式的事件"""
        print(f"[DEBUG] Generating flamegraph events for PID {card_data['pid']}")
        events = []
        current_time = 0
        
        # 按 stage_id 排序处理
        sorted_groups = sorted(
            card_data["alloc_groups"].items(),
            key=lambda x: x[0][1]  # 按 stage_id 排序
        )
        
        for (stage_type, stage_id), allocs in sorted_groups:
            # 构建调用树
            call_tree = self._build_call_tree(allocs, stage_type)
            
            # 生成事件块
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

    def _build_call_tree(self, allocations, stage_type):
        """构建合并后的调用树"""
        root = {
            "name": self.stage_names.get(stage_type, "UNKNOWN"),
            "children": {},
            "size": 0
        }
        
        # 合并相同调用路径的分配
        for alloc in allocations:
            current = root
            path = [
                f"{frame.so_name}@{hex(frame.address)}" 
                for frame in alloc.stack_frames
            ]
            
            for frame in path:
                if frame not in current["children"]:
                    current["children"][frame] = {
                        "name": frame,
                        "children": {},
                        "size": 0
                    }
                current = current["children"][frame]
            current["size"] += alloc.mem_size
        
        # 计算子树总大小
        self._compute_tree_sizes(root)
        return root

    def _compute_tree_sizes(self, node):
        """递归计算子树总大小"""
        if not node["children"]:
            return node["size"]
        
        total = 0
        for child in node["children"].values():
            total += self._compute_tree_sizes(child)
        
        node["size"] = total
        return total

    def _create_events_from_tree(self, tree, pid, start_time, stage_type, stage_id):
        """从调用树生成事件"""
        events = []
        
        def _traverse(node, ts, depth):
            events.append({
                "name": node["name"],
                "ph": "X",  # 持续时间事件
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
            
            # 按名称排序子节点保证一致性
            child_start = ts
            for child in sorted(node["children"].values(), key=lambda x: x["name"]):
                _traverse(child, child_start, depth + 1)
                child_start += child["size"]
        
        _traverse(tree, start_time, 0)
        return events

    def _save_json(self, path, events):
        """保存为Chrome tracing格式的JSON"""
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
