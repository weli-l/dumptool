# flamegraph_converter.py
import sys
import file_pb2  # 假设这是编译后的proto文件
import flamegraph

def parse_proto_to_flamegraph(input_file, output_file):
    with open(input_file, 'rb') as f:
        data = file_pb2.ProcMemStack()
        data.ParseFromString(f.read())
    
    stack_counts = {}
    
    for mem_stack in data.mem_stack:
        if not mem_stack.is_alloc:  # 只处理分配操作
            continue
            
        stack = []
        for frame in reversed(mem_stack.stack_frames):
            if frame.so_name:
                stack.append(f"{frame.so_name}+0x{frame.address:x}")
            else:
                stack.append(f"0x{frame.address:x}")
        
        stack_str = ';'.join(stack)
        stack_counts[stack_str] = stack_counts.get(stack_str, 0) + mem_stack.mem_size
    
    flamegraph.flamegraph(stack_counts, output_file)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python flamegraph_converter.py <input.proto> <output.svg>")
        sys.exit(1)
    
    parse_proto_to_flamegraph(sys.argv[1], sys.argv[2])
