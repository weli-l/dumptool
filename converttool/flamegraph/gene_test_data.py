#!/usr/bin/env python3
from mem_profile_pb2 import Mem, ProcMem, MemAllocEntry, StackFrame, MemFreeEntry

def create_test_data():
    """创建多阶段混合的多分支测试数据"""
    mem = Mem()
    proc = ProcMem(pid=0)  # 单卡测试（卡号0）

    # ----------------------------
    # 阶段0: DataLoader (stage_id=0)
    # ----------------------------
    # 分支1: dl_main -> loader -> io_task
    alloc1 = MemAllocEntry(
        alloc_ptr=0x1000,
        stage_id=0,
        mem_size=800,
        stack_frames=[
            StackFrame(address=0x1000, so_name="dl_main"),
            StackFrame(address=0x2000, so_name="loader"),
            StackFrame(address=0x3000, so_name="io_task")
        ]
    )
    # 分支2: dl_main -> loader -> decode
    alloc2 = MemAllocEntry(
        alloc_ptr=0x2000,
        stage_id=0,
        mem_size=400,
        stack_frames=[
            StackFrame(address=0x1000, so_name="dl_main"),
            StackFrame(address=0x2000, so_name="loader"),
            StackFrame(address=0x4000, so_name="decode")
        ]
    )

    # ----------------------------
    # 阶段1: Forward (stage_id=1)
    # ----------------------------
    # 分支3: main -> libtorch -> conv2d
    alloc3 = MemAllocEntry(
        alloc_ptr=0x3000,
        stage_id=1,
        mem_size=1200,
        stack_frames=[
            StackFrame(address=0x5000, so_name="main"),
            StackFrame(address=0x6000, so_name="libtorch"),
            StackFrame(address=0x7000, so_name="conv2d")
        ]
    )
    # 分支4: main -> libtorch -> linear
    alloc4 = MemAllocEntry(
        alloc_ptr=0x4000,
        stage_id=1,
        mem_size=600,
        stack_frames=[
            StackFrame(address=0x5000, so_name="main"),
            StackFrame(address=0x6000, so_name="libtorch"),
            StackFrame(address=0x8000, so_name="linear")
        ]
    )
    # 释放alloc3的部分内存
    free1 = MemFreeEntry(alloc_ptr=0x3000)

    # ----------------------------
    # 阶段2: Backward (stage_id=2)
    # ----------------------------
    # 分支5: main -> autograd -> backward
    alloc5 = MemAllocEntry(
        alloc_ptr=0x5000,
        stage_id=2,
        mem_size=900,
        stack_frames=[
            StackFrame(address=0x5000, so_name="main"),
            StackFrame(address=0x9000, so_name="autograd"),
            StackFrame(address=0xA000, so_name="backward")
        ]
    )
    # 分支6: main -> optimizer -> step
    alloc6 = MemAllocEntry(
        alloc_ptr=0x6000,
        stage_id=2,
        mem_size=300,
        stack_frames=[
            StackFrame(address=0x5000, so_name="main"),
            StackFrame(address=0xB000, so_name="optimizer"),
            StackFrame(address=0xC000, so_name="step")
        ]
    )

    proc.mem_alloc_stacks.extend([alloc1, alloc2, alloc3, alloc4, alloc5, alloc6])
    proc.mem_free_stacks.append(free1)
    mem.proc_mem.append(proc)
    return mem

def save_to_file(mem_data, filename):
    with open(filename, "wb") as f:
        f.write(mem_data.SerializeToString())

if __name__ == "__main__":
    test_data = create_test_data()
    save_to_file(test_data, "multistage_test.bin")
    print("测试数据已生成: multistage_test.bin")
    print("\n预期调用树结构:")
    print("""
    [STAGE_DATALOADER] (1200B)
    ├── [dl_main@0x1000] (1200B)
    │   ├── [loader@0x2000] (1200B)
    │   │   ├── [io_task@0x3000] (800B)
    │   │   └── [decode@0x4000] (400B)
    
    [STAGE_FORWARD] (600B)  # alloc3被释放，仅剩alloc4
    ├── [main@0x5000] (600B)
    │   ├── [libtorch@0x6000] (600B)
    │   │   └── [linear@0x8000] (600B)
    
    [STAGE_BACKWARD] (1200B)
    ├── [main@0x5000] (1200B)
    │   ├── [autograd@0x9000] (900B)
    │   │   └── [backward@0xA000] (900B)
    │   └── [optimizer@0xB000] (300B)
    │       └── [step@0xC000] (300B)
    """)
