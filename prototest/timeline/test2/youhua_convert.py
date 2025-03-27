import json
import pytorch_pb2
from typing import Iterable, Dict
import sys

def generate_events(pytorch_data: pytorch_pb2.Pytorch) -> Iterable[Dict]:
    """流式生成事件数据"""
    for stage in pytorch_data.pytorch_stages:
        yield {
            "name": pytorch_pb2.StageType.Name(stage.stage_type),
            "cat": stage.comm,
            "ph": "X",
            "ts": stage.start_us,
            "dur": stage.end_us - stage.start_us,
            "pid": stage.rank,
            "args": {
                "stage_id": stage.stage_id,
                "step_id": stage.step_id,
                "stack_frames": [
                    {"address": hex(frame.address), "so_name": frame.so_name}
                    for frame in stage.stack_frames
                ]
            }
        }

def stream_convert(pytorch_data: pytorch_pb2.Pytorch, output_file: str):
    """流式写入JSON文件"""
    with open(output_file, 'w', buffering=4096 * 1024) as f:  # 4MB缓冲
        f.write('{"traceEvents": [\n')
        
        first = True
        for event in generate_events(pytorch_data):
            if not first:
                f.write(",\n")
            else:
                first = False
            json.dump(event, f)
            
        f.write('\n], "displayTimeUnit": "ms", "metadata": {"format": "Perfetto Chrome JSON"}}')

def load_from_binary(filename: str) -> pytorch_pb2.Pytorch:
    pytorch_data = pytorch_pb2.Pytorch()
    with open(filename, 'rb', buffering=0) as f:  # 禁用缓冲
        pytorch_data.ParseFromString(f.read())
    return pytorch_data

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python convert_to_chrome_json.py <input.bin> <output.json>")
        sys.exit(1)

    pytorch_data = load_from_binary(sys.argv[1])
    stream_convert(pytorch_data, sys.argv[2])
    print(f"Converted {sys.argv[1]} to Chrome JSON format in {sys.argv[2]}")
