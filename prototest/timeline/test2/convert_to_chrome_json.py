import json
import pytorch_pb2
from typing import Dict, List

def stage_type_to_name(stage_type: int) -> str:
    return pytorch_pb2.StageType.Name(stage_type)

def convert_to_chrome_json(pytorch_data: pytorch_pb2.Pytorch) -> Dict:
    events = []
    for stage in pytorch_data.pytorch_stages:
        event = {
            "name": stage_type_to_name(stage.stage_type),
            "cat": stage.comm,
            "ph": "X",
            "ts": stage.start_us,
            "dur": stage.end_us - stage.start_us,
            "pid": stage.rank,
            "args": {
                "stage_id": stage.stage_id,
                "step_id": stage.step_id,
                "stack_frames": [
                    {
                        "address": hex(frame.address),
                        "so_name": frame.so_name
                    }
                    for frame in stage.stack_frames
                ]
            }
        }
        events.append(event)
    return {
        "traceEvents": events,
        "displayTimeUnit": "ms",
        "metadata": {
            "format": "Perfetto Chrome JSON"
        }
    }

def load_from_binary(filename: str) -> pytorch_pb2.Pytorch:
    pytorch_data = pytorch_pb2.Pytorch()
    with open(filename, 'rb') as f:
        pytorch_data.ParseFromString(f.read())
    return pytorch_data

if __name__ == "__main__":
    import sys

    if len(sys.argv) != 3:
        print("Usage: python convert_to_chrome_json.py <input.bin> <output.json>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    pytorch_data = load_from_binary(input_file)
    chrome_json = convert_to_chrome_json(pytorch_data)

    with open(output_file, 'w') as f:
        json.dump(chrome_json, f, indent=2)

    print(f"Converted {input_file} to Chrome JSON format in {output_file}")
