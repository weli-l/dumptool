import json
import timeline_pb2
from typing import Dict, List

def stage_type_to_name(stage_type: int) -> str:
    return timeline_pb2.StageType.Name(stage_type)

def convert_to_chrome_json(timeline: timeline_pb2.Timeline) -> Dict:
    events = []
    for stage in timeline.stages:
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

def load_from_binary(filename: str) -> timeline_pb2.Timeline:
    timeline = timeline_pb2.Timeline()
    with open(filename, 'rb') as f:
        timeline.ParseFromString(f.read())
    return timeline

if __name__ == "__main__":
    import sys

    if len(sys.argv) != 3:
        print("Usage: python convert_to_chrome_json.py <input.bin> <output.json>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    timeline = load_from_binary(input_file)
    chrome_json = convert_to_chrome_json(timeline)

    with open(output_file, 'w') as f:
        json.dump(chrome_json, f, indent=2)

    print(f"Converted {input_file} to Chrome JSON format in {output_file}")
