#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import random
import mem_profile_pb2

def generate_test_binary(output_file="memory_deep_test.bin"):
    """生成包含深层调用栈的测试数据"""
    mem = mem_profile_pb2.Mem()
    proc_mem = mem.proc_mem.add()
    proc_mem.pid = 1234
    
    # 内存分配特征配置
    stage_config = {
        "dataloader": {
            "alloc_range": (3, 5), 
            "free_ratio": 0.3,
            "size_base": 1 * 1024 * 1024,
            "stack_depth": (4, 6)  # 深层调用栈
        },
        "forward": {
            "alloc_range": (4, 6),
            "free_ratio": 0.1,  # 更多泄漏
            "size_base": 2 * 1024 * 1024,
            "stack_depth": (5, 7)  # 更深的调用栈
        },
        "backward": {
            "alloc_range": (2, 4),
            "free_ratio": 0.5,
            "size_base": 1.5 * 1024 * 1024,
            "stack_depth": (3, 5)
        }
    }
    
    # 生成3个完整周期
    for cycle in range(3):
        for stage_id, stage_name in enumerate(["dataloader", "forward", "backward"]):
            proc_mem.stage_type = stage_id
            config = stage_config[stage_name]
            
            # 生成alloc和free
            alloc_count = random.randint(*config["alloc_range"])
            free_count = int(alloc_count * config["free_ratio"])
            
            add_alloc_entries(
                proc_mem, 
                stage_id=stage_id,
                alloc_count=alloc_count,
                free_count=free_count,
                base_ptr=0x10000000 * (stage_id + 1) + cycle * 0x1000000,
                base_size=config["size_base"],
                stack_depth_range=config["stack_depth"]
            )
    
    # 写入二进制文件
    with open(output_file, "wb") as f:
        f.write(mem.SerializeToString())
    print(f"已生成深层调用测试文件: {output_file}")

def add_alloc_entries(proc_mem, stage_id, alloc_count, free_count, base_ptr, base_size, stack_depth_range):
    """添加带有深层调用栈的内存分配记录"""
    alloc_ptrs = []
    
    # 预定义各阶段的调用栈模板
    stack_templates = {
        0: [  # dataloader
            ["DataLoader", "BatchSampler", "TensorLoader", "FileReader", "Decryptor"],
            ["Prefetcher", "Transform", "Normalize", "Augmentation"]
        ],
        1: [  # forward
            ["Model", "Sequential", "ConvLayer", "Activation", "Normalization"],
            ["Transformer", "Attention", "FeedForward", "LayerNorm"]
        ],
        2: [  # backward
            ["Optimizer", "GradCalc", "Backprop", "WeightUpdate"],
            ["LossFn", "Reduction", "Derivative"]
        ]
    }
    
    for i in range(alloc_count):
        alloc = proc_mem.mem_alloc_stacks.add()
        alloc.alloc_ptr = base_ptr + i * 0x100000
        alloc.mem_size = int(base_size * (i + 1 + random.random()))  # 带随机性的递增
        alloc.stage_id = stage_id
        
        # 生成深层调用栈
        stack_depth = random.randint(*stack_depth_range)
        call_stack = []
        
        # 从模板中选择调用路径
        template = random.choice(stack_templates[stage_id])
        for d in range(stack_depth):
            frame = alloc.stack_frames.add()
            if d < len(template):
                func_name = template[d]
            else:
                func_name = f"internal_{d}"
            
            frame.so_name = f"lib{func_name.lower()}.so"
            frame.address = 0x1000 * (i + 1) + 0x100 * d
            call_stack.append(func_name)
        
        alloc_ptrs.append(alloc.alloc_ptr)
    
    # 添加free记录
    for ptr in random.sample(alloc_ptrs, min(free_count, len(alloc_ptrs))):
        free = proc_mem.mem_free_stacks.add()
        free.alloc_ptr = ptr

if __name__ == "__main__":
    generate_test_binary()
