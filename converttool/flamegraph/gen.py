#!/usr/bin/env python3
"""
gen_large_fixed.py - 修正版大数据生成脚本
严格匹配proto定义，确保与转换脚本兼容
"""

import random
import time
from mem_profile_pb2 import Mem, ProcMem, MemAlloc, MemFree, StackFrame

def generate_stack_frame():
    frame = StackFrame()
    frame.so_name = random.choice([
        "libcuda.so", 
        "libtorch.so",
        "libdataloader.so"
    ])
    frame.address = random.randint(0x1000, 0xFFFFF)
    return frame

def generate_alloc(card_id):
    alloc = MemAlloc()
    alloc.alloc_ptr = random.randint(0x100000, 0xFFFFFFFF)
    alloc.mem_size = random.randint(1024, 1024 * 1024)  # 1KB-1MB
    
    # 关键修正：确保stage_type与转换脚本预期一致
    stage_id = random.choice([0, 1, 2, 10, 11, 12])
    alloc.stage_id = stage_id
    alloc.stage_type = ["DATA_LOAD", "FORWARD", "BACKWARD"][stage_id % 3]
    
    # 生成3-5个栈帧
    alloc.stack_frames.extend([generate_stack_frame() for _ in range(random.randint(3, 5))])
    return alloc

def generate_perf_data(total_records=1_000_000):
    mem = Mem()
    
    # 生成4张卡的数据
    for card_id in [1001, 1002, 1003, 1004]:
        proc_mem = ProcMem()
        proc_mem.pid = card_id
        
        print(f"Generating card {card_id}...")
        start = time.time()
        
        # 生成分配记录
        allocs = [generate_alloc(card_id) for _ in range(total_records // 4)]
        proc_mem.mem_alloc_stacks.extend(allocs)
        
        # 生成释放记录（约30%）
        for alloc in random.sample(allocs, int(len(allocs) * 0.3)):
            free = MemFree()
            free.alloc_ptr = alloc.alloc_ptr
            free.stack_frames.extend(random.sample(alloc.stack_frames, random.randint(1, 3)))
            proc_mem.mem_free_stacks.append(free)
        
        mem.proc_mem.append(proc_mem)
        print(f"Card {card_id} done in {time.time()-start:.2f}s")
    
    return mem

def save_to_file(data, filename):
    with open(filename, "wb") as f:
        f.write(data.SerializeToString())

if __name__ == "__main__":
    output = "large_data_fixed.bin"
    print(f"Generating 10M records to {output}...")
    
    start_time = time.time()
    data = generate_perf_data()
    save_to_file(data, output)
    
    # 验证统计信息
    total_allocs = sum(len(p.mem_alloc_stacks) for p in data.proc_mem)
    total_frees = sum(len(p.mem_free_stacks) for p in data.proc_mem)
    
    print(f"\nGeneration complete in {time.time()-start_time:.2f}s")
    print(f"Total allocations: {total_allocs:,}")
    print(f"Total frees: {total_frees:,}")
    print(f"Output file: {output}")
