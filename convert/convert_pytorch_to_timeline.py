import json
import systrace_pb2
import argparse
import glob

def proto_to_json(input_path, output_path=None, trace_data=None):
    with open(input_path, "rb") as f:
        pytorch_data = systrace_pb2.Pytorch()
        pytorch_data.ParseFromString(f.read())
    
    if trace_data is None:
        trace_data = {
            "traceEvents": [],
            "displayTimeUnit": "ns",
            "metadata": {
                "format": "Pytorch Profiler",
                "rank": pytorch_data.rank,
                "step": pytorch_data.step_id
            }
        }
        output_needed = True
    else:
        output_needed = False
    
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
    
    # 按照stage_id排序
    trace_data["traceEvents"].sort(key=lambda x: x["args"]["stage_id"])
    
    if output_needed:
        with open(output_path, "w") as f:
            json.dump(trace_data, f, indent=None, separators=(',', ':'))
        return None
    else:
        return trace_data

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Convert PyTorch protobuf trace to JSON format')
    parser.add_argument('--input', help='Input protobuf file path (single file mode)')
    parser.add_argument('--output', help='Output JSON file path (required for single file mode)')
    parser.add_argument('--aggregate', action='store_true', help='Aggregate all *timeline files in current directory')
    parser.add_argument('--aggregate-output', default='aggregated.json', help='Output path for aggregated result')
    
    args = parser.parse_args()
    
    if args.aggregate:
        all_timeline_files = glob.glob("*timeline")
        if not all_timeline_files:
            print("No *timeline files found in current directory")
            exit(1)
            
        aggregated_data = None
        for timeline_file in all_timeline_files:
            print(f"Processing {timeline_file}")
            aggregated_data = proto_to_json(timeline_file, trace_data=aggregated_data)
        
        with open(args.aggregate_output, "w") as f:
            json.dump(aggregated_data, f, indent=None, separators=(',', ':'))
        print(f"Aggregated data saved to {args.aggregate_output}")
    elif args.input and args.output:
        proto_to_json(args.input, args.output)
    else:
        parser.print_help()