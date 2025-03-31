# generate_test_data_final.py
import sys
from mem_pb2 import Mem, ProcMem, MemAllocEntry, MemFreeEntry, StackFrame

def create_test_data():
    """创建符合所有规范的测试数据"""
    mem = Mem()

    # 卡0的数据 (3个阶段)
    proc0 = ProcMem(pid=0)  # 卡号0

    # DataLoader阶段 (stage_id=0)
    alloc1 = MemAllocEntry(
        alloc_ptr=0x1000,
        stage_id=0,  # DataLoader
        mem_size=1024,
        stack_frames=[
            StackFrame(address=0x4000, so_name="dataloader"),
            StackFrame(address=0x5000, so_name="libtorch.so")
        ]
    )

    # Forward阶段 (stage_id=1)
    alloc2 = MemAllocEntry(
        alloc_ptr=0x2000,
        stage_id=1,  # Forward
        mem_size=2048,
        stack_frames=[
            StackFrame(address=0x4000, so_name="main"),
            StackFrame(address=0x6000, so_name="libcuda.so"),
            StackFrame(address=0x7000, so_name="kernel")
        ]
    )

    # Backward阶段 (stage_id=2) - 将被部分释放
    alloc3 = MemAllocEntry(
        alloc_ptr=0x3000,
        stage_id=2,  # Backward
        mem_size=1536,
        stack_frames=[
            StackFrame(address=0x8000, so_name="main"),
            StackFrame(address=0x9000, so_name="autograd"),
            StackFrame(address=0xA000, so_name="kernel")
        ]
    )
    alloc4 = MemAllocEntry(
        alloc_ptr=0x4000,
        stage_id=2,
        mem_size=512,
        stack_frames=[
            StackFrame(address=0x8000, so_name="main"),
            StackFrame(address=0xB000, so_name="optimizer")
        ]
    )

    # 释放操作 (释放alloc3的部分内存)
    free1 = MemFreeEntry(alloc_ptr=0x3000)

    proc0.mem_alloc_stacks.extend([alloc1, alloc2, alloc3, alloc4])
    proc0.mem_free_stacks.extend([free1])

    # 卡1的数据 (仅Forward阶段)
    proc1 = ProcMem(pid=1)  # 卡号1
    alloc5 = MemAllocEntry(
        alloc_ptr=0x5000,
        stage_id=1,  # Forward
        mem_size=3072,
        stack_frames=[
            StackFrame(address=0xC000, so_name="main"),
            StackFrame(address=0xD000, so_name="libtorch.so"),
            StackFrame(address=0xE000, so_name="custom_op")
        ]
    )
    proc1.mem_alloc_stacks.extend([alloc5])

    mem.proc_mem.extend([proc0, proc1])
    return mem

def save_to_file(mem_data, filename):
    """保存为二进制文件"""
    with open(filename, "wb") as f:
        f.write(mem_data.SerializeToString())

def print_debug_info(mem_data):
    """打印调试信息"""
    print("\nGenerated test data structure:")
    for i, proc in enumerate(mem_data.proc_mem):
        print(f"\nCard {proc.pid}:")
        print(f"  Allocations: {len(proc.mem_alloc_stacks)}")
        for j, alloc in enumerate(proc.mem_alloc_stacks, 1):
            print(f"    Alloc {j}: ptr=0x{alloc.alloc_ptr:x} stage={alloc.stage_id} size={alloc.mem_size}")
            print("      Call stack:", " -> ".join(
                f"{f.so_name}@0x{f.address:x}" for f in alloc.stack_frames
            ))
        
        print(f"  Frees: {len(proc.mem_free_stacks)}")
        for j, free in enumerate(proc.mem_free_stacks, 1):
            print(f"    Free {j}: ptr=0x{free.alloc_ptr:x}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python generate_test_data_final.py <output.bin>")
        sys.exit(1)

    test_data = create_test_data()
    save_to_file(test_data, sys.argv[1])
    print_debug_info(test_data)
    print(f"\nTest data saved to {sys.argv[1]}")
