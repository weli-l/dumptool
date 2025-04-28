import json
import systrace_pb2
import argparse

def proto_to_json(input_path, output_path):
    with open(input_path, "rb") as f:
        pytorch_data = systrace_pb2.Pytorch()
        pytorch_data.ParseFromString(f.read())
    
    trace_data = {
        "traceEvents": [],
        "displayTimeUnit": "ns",
        "metadata": {
            "format": "Pytorch Profiler",
            "rank": pytorch_data.rank,
            "step": pytorch_data.step_id
        }
    }
    
    trace_data["traceEvents"].append({
        "name": "process_name",
        "ph": "M",
        "pid": pytorch_data.rank,
        "tid": 0,
        "args": {
            "name": f"Rank {pytorch_data.rank}"
        }
    })
    
    for stage in pytorch_data.pytorch_stages:
        start_us = stage.start_us
        duration_us = stage.end_us - stage.start_us
        
        trace_data["traceEvents"].append({
            "name": stage.stage_type,
            "cat": "pytorch",
            "ph": "X",
            "pid": pytorch_data.rank,
            "tid": pytorch_data.rank,
            "ts": start_us,
            "dur": duration_us,
            "args": {
                "stage_id": stage.stage_id,
                "stack_frames": list(stage.stack_frames),
                "gc_collected": stage.gc_debug.collected if stage.HasField("gc_debug") else 0,
                "gc_uncollectable": stage.gc_debug.uncollectable if stage.HasField("gc_debug") else 0
            }
        })
        
        trace_data["traceEvents"].append({
            "name": "thread_name",
            "ph": "M",
            "pid": pytorch_data.rank,
            "tid": stage.stage_id,
            "args": {
                "name": f"Stage {stage.stage_id}"
            }
        })
    
    with open(output_path, "w") as f:
        json.dump(trace_data, f, indent=None, separators=(',', ':'))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Convert PyTorch protobuf trace to JSON format')
    parser.add_argument('input', help='Input protobuf file path')
    parser.add_argument('output', help='Output JSON file path')
    args = parser.parse_args()
    
    proto_to_json(args.input, args.output)