#!/usr/bin/env python3
import json
import sys
from collections import defaultdict
from mem_profile_pb2 import Mem

def convert_to_chrome_tracing(input_bin, output_json):
    mem_data = Mem()
    with open(input_bin, "rb") as f:
        mem_data.ParseFromString(f.read())

    events = []
    tid = 1
    pid = 1
    base_ts = 100000  # 基础时间戳

    # 调用树数据结构
    call_tree = defaultdict(lambda: {
        'self_size': 0,          # 自身分配的内存
        'children': defaultdict(int),  # {子函数: 调用次数}
        'child_total': 0         # 直接子函数内存总和（不含自身）
    })

    # 过滤已释放的内存
    freed_ptrs = set()
    for proc in mem_data.proc_mem:
        for free in proc.mem_free_stacks:
            freed_ptrs.add(free.alloc_ptr)

    # 构建精确调用关系
    for proc in mem_data.proc_mem:
        for alloc in proc.mem_alloc_stacks:
            if alloc.alloc_ptr in freed_ptrs:
                continue
            
            stack = [f.so_name for f in alloc.stack_frames]
            for i in range(len(stack)):
                # 记录自身内存
                if i == len(stack)-1:  # 栈底函数
                    call_tree[stack[i]]['self_size'] += alloc.mem_size
                
                # 记录调用关系
                if i < len(stack)-1:
                    parent = stack[i]
                    child = stack[i+1]
                    call_tree[parent]['children'][child] += 1

    # 计算子函数总内存（关键修正）
    def calculate_child_total(func):
        total = 0
        for child, count in call_tree[func]['children'].items():
            # 子函数内存 = 其自身内存 + 其子函数内存
            child_size = (call_tree[child]['self_size'] + calculate_child_total(child)) * count
            total += child_size
        call_tree[func]['child_total'] = total
        return total

    if 'main::main()' in call_tree:
        calculate_child_total('main::main()')

    # 生成事件（严格垂直+水平布局）
    def generate_events(func, depth, start_ts):
        # 子函数总内存
        child_total = call_tree[func]['child_total']
        
        # 自身内存事件（右侧显示）
        if call_tree[func]['self_size'] > 0:
            self_start = start_ts + child_total
            events.append({
                "name": f"{func} (self)",
                "ph": "B",
                "ts": self_start,
                "pid": pid,
                "tid": tid,
                "args": {"depth": depth, "bytes": call_tree[func]['self_size']}
            })
            events.append({
                "name": f"{func} (self)",
                "ph": "E",
                "ts": self_start + call_tree[func]['self_size'],
                "pid": pid,
                "tid": tid
            })

        # 父函数事件（覆盖子函数）
        events.append({
            "name": func,
            "ph": "B",
            "ts": start_ts,
            "pid": pid,
            "tid": tid,
            "args": {"depth": depth, "bytes": child_total}
        })

        # 子函数水平排列（紧贴无间隙）
        child_start = start_ts
        for child, count in call_tree[func]['children'].items():
            for _ in range(count):
                # 子函数总宽度 = 自身内存 + 子子函数内存
                child_size = call_tree[child]['self_size'] + call_tree[child]['child_total']
                generate_events(child, depth + 1, child_start)
                child_start += child_size  # 下一个子函数紧贴前一个

        events.append({
            "name": func,
            "ph": "E",
            "ts": start_ts + child_total,
            "pid": pid,
            "tid": tid
        })

    # 生成事件
    if 'main::main()' in call_tree:
        generate_events('main::main()', 0, base_ts)

    # 保存结果
    with open(output_json, "w") as f:
        json.dump({
            "traceEvents": sorted(events, key=lambda x: x["ts"]),
            "displayTimeUnit": "ns"
        }, f, indent=2)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: ./convert_to_chrome_tracing.py input.bin output.json")
        sys.exit(1)
    convert_to_chrome_tracing(sys.argv[1], sys.argv[2])
    print(f"转换完成，结果已保存到 {sys.argv[2]}")
