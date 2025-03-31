#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import json
from collections import defaultdict
import mem_profile_pb2

def generate_tracing_data(proc_mem):
    """生成完全符合Chrome Tracing格式的火焰图数据"""
    events = []
    timestamp = 0
    
    # 1. 添加元数据事件
    events.extend([
        {
            "name": "process_name",
            "ph": "M",
            "pid": proc_mem.pid,
            "tid": 1,
            "args": {"name": "AI Training"}
        },
        {
            "name": "thread_name",
            "ph": "M",
            "pid": proc_mem.pid,
            "tid": 1,
            "args": {"name": "Main Thread"}
        }
    ])
    
    # 2. 处理alloc/free
    active_allocs = {}
    for free_entry in proc_mem.mem_free_stacks:
        if free_entry.alloc_ptr in active_allocs:
            del active_allocs[free_entry.alloc_ptr]
    
    # 3. 按阶段分组并构建调用树
    stage_trees = defaultdict(lambda: {"children": defaultdict(int), "total": 0})
    
    for alloc_entry in proc_mem.mem_alloc_stacks:
        if alloc_entry.alloc_ptr in active_allocs:
            continue
        
        stage_name = {
            0: "dataloader",
            1: "forward",
            2: "backward"
        }[alloc_entry.stage_id]
        
        # 构建调用栈路径
        stack = []
        for frame in alloc_entry.stack_frames:
            func_name = f"{frame.so_name.split('.')[0]}"
            stack.append(func_name)
        
        if not stack:
            stack = ["unknown"]
        
        # 更新调用树
        parent_path = stage_name
        stage_trees[stage_name]["total"] += alloc_entry.mem_size
        
        for i, func in enumerate(stack):
            current_path = f"{parent_path}|{func}" if i > 0 else f"{stage_name}|{func}"
            is_leaf = (i == len(stack)-1)
            
            if is_leaf:
                stage_trees[stage_name]["children"][current_path] += alloc_entry.mem_size
            parent_path = current_path
    
    # 4. 生成Chrome Tracing事件
    # 先添加阶段标记
    stage_positions = {}
    for i, stage_name in enumerate(["dataloader", "forward", "backward"]):
        if stage_name not in stage_trees:
            continue
        
        stage_positions[stage_name] = timestamp
        events.append({
            "name": stage_name,
            "ph": "X",
            "ts": timestamp,
            "dur": stage_trees[stage_name]["total"],
            "pid": proc_mem.pid,
            "tid": 1,
            "cat": "stage",
            "args": {}
        })
        timestamp += stage_trees[stage_name]["total"] + 1000
    
    # 添加函数调用事件
    for stage_name, tree in stage_trees.items():
        start_time = stage_positions[stage_name]
        
        # 按路径长度排序，确保先处理父节点
        sorted_paths = sorted(tree["children"].keys(), 
                            key=lambda x: x.count("|"))
        
        for path in sorted_paths:
            parts = path.split("|")
            func_name = parts[-1]
            parent_path = "|".join(parts[:-1]) if len(parts) > 1 else stage_name
            duration = tree["children"][path]
            
            # 开始事件
            events.append({
                "name": func_name,
                "ph": "B",
                "ts": start_time,
                "pid": proc_mem.pid,
                "tid": 1,
                "cat": "function",
                "args": {
                    "stage": stage_name,
                    "bytes": duration,
                    "parent": parent_path if parent_path != stage_name else None
                }
            })
            
            # 结束事件
            events.append({
                "name": func_name,
                "ph": "E",
                "ts": start_time + duration,
                "pid": proc_mem.pid,
                "tid": 1
            })
            
            start_time += duration
    
    return events

def main(input_file, output_file):
    # 读取proto文件
    with open(input_file, "rb") as f:
        data = f.read()
    
    mem = mem_profile_pb2.Mem()
    mem.ParseFromString(data)
    
    # 生成所有事件
    all_events = []
    for proc_mem in mem.proc_mem:
        all_events.extend(generate_tracing_data(proc_mem))
    
    # 写入JSON文件
    with open(output_file, "w") as f:
        json.dump({"traceEvents": all_events}, f, indent=2)
    
    print(f"成功生成Chrome Tracing文件: {output_file}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("用法: python proto_to_tracing.py <input.bin> <output.json>")
        sys.exit(1)
    
    main(sys.argv[1], sys.argv[2])
