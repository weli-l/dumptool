# proto1_writer.py
import file_pb2
import random
import sys

def generate_sample_data():
    proc_mem_stack = file_pb2.ProcMemStack()
    proc_mem_stack.pid = 1234
    
    for i in range(100):  # 生成100个内存操作样本
        mem_stack = proc_mem_stack.mem_stack.add()
        mem_stack.alloc_ptr = random.randint(0x10000000, 0xffffffff)
        mem_stack.stage_id = i % 10
        mem_stack.mem_size = random.randint(8, 1024)
        mem_stack.is_alloc = random.choice([True, False])
        
        # 生成调用栈(3-5层)
        stack_depth = random.randint(3, 5)
        for j in range(stack_depth):
            frame = mem_stack.stack_frames.add()
            frame.address = random.randint(0x00005555, 0xffffffff)
            frame.so_name = random.choice(["libc.so.6", "libpython3.8.so", "myapp", "", "kernel"])
    
    return proc_mem_stack

def write_proto1_binary(output_file):
    data = generate_sample_data()
    with open(output_file, 'wb') as f:
        f.write(data.SerializeToString())

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python proto1_writer.py <output.bin>")
        sys.exit(1)
    
    write_proto1_binary(sys.argv[1])
