#!/usr/bin/env python3
from mem_profile_pb2 import Mem, ProcMem, MemAllocEntry, StackFrame

def create_test_data():
    """创建多分支调用栈的测试数据"""
    mem = Mem()
    proc = ProcMem(pid=0)  # 单卡测试（卡号0）

    # 阶段1: Forward (stage_id=1)
    # 分支1: main -> libA -> funcX
    alloc1 = MemAllocEntry(
        alloc_ptr=0x1000,
        stage_id=1,
        mem_size=1024,
        stack_frames=[
            StackFrame(address=0x4000, so_name="main"),
            StackFrame(address=0x5000, so_name="libA"),
            StackFrame(address=0x6000, so_name="funcX")
        ]
    )

    # 分支2: main -> libA -> funcY
    alloc2 = MemAllocEntry(
        alloc_ptr=0x2000,
        stage_id=1,
        mem_size=512,
        stack_frames=[
            StackFrame(address=0x4000, so_name="main"),
            StackFrame(address=0x5000, so_name="libA"),
            StackFrame(address=0x7000, so_name="funcY")
        ]
    )

    # 分支3: main -> libB -> kernel1
    alloc3 = MemAllocEntry(
        alloc_ptr=0x3000,
        stage_id=1,
        mem_size=768,
        stack_frames=[
            StackFrame(address=0x4000, so_name="main"),
            StackFrame(address=0x8000, so_name="libB"),
            StackFrame(address=0x9000, so_name="kernel1")
        ]
    )

    # 分支4: main -> libB -> kernel2
    alloc4 = MemAllocEntry(
        alloc_ptr=0x4000,
        stage_id=1,
        mem_size=256,
        stack_frames=[
            StackFrame(address=0x4000, so_name="main"),
            StackFrame(address=0x8000, so_name="libB"),
            StackFrame(address=0xA000, so_name="kernel2")
        ]
    )

    proc.mem_alloc_stacks.extend([alloc1, alloc2, alloc3, alloc4])
    mem.proc_mem.append(proc)
    return mem

def save_to_file(mem_data, filename):
    with open(filename, "wb") as f:
        f.write(mem_data.SerializeToString())

if __name__ == "__main__":
    test_data = create_test_data()
    save_to_file(test_data, "multibranch_test.bin")
    print("测试数据已生成: multibranch_test.bin")
    print("\n调用树结构预览:")
    print("""
    [STAGE_FORWARD] (2560B)
    ├── [main@0x4000] (2560B)
    │   ├── [libA@0x5000] (1536B)
    │   │   ├── [funcX@0x6000] (1024B)
    │   │   └── [funcY@0x7000] (512B)
    │   └── [libB@0x8000] (1024B)
    │       ├── [kernel1@0x9000] (768B)
    │       └── [kernel2@0xA000] (256B)
    """)
