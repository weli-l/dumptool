import json
import trace_pb2

def proto_to_json(input_path, output_path):
    # Read Protobuf data
    with open(input_path, "rb") as f:
        pytorch_data = trace_pb2.Pytorch()
        pytorch_data.ParseFromString(f.read())
    
    # Build JSON structure for Chrome Tracing format
    result = {
        "traceEvents": [],
        "displayTimeUnit": "ns",
        "otherData": {
            "version": "PyTorch Memory Profiler",
            "rank": pytorch_data.rank,
            "step_id": pytorch_data.step_id,
            "comm": pytorch_data.comm
        }
    }
    
    # Convert Pytorch stages to trace events
    for stage in pytorch_data.pytorch_stages:
        # Main stage event
        stage_event = {
            "name": f"{stage.stage_type} (stage_id={stage.stage_id})",
            "cat": "pytorch",
            "ph": "X",  # Complete event
            "pid": pytorch_data.rank,
            "tid": stage.stage_id,
            "ts": stage.start_us * 1000,  # Convert microseconds to nanoseconds
            "dur": (stage.end_us - stage.start_us) * 1000,
            "args": {
                "stage_type": stage.stage_type,
                "stack_frames": stage.stack_frames
            }
        }
        
        # Add GC debug data if present
        if stage.HasField("gc_debug"):
            stage_event["args"].update({
                "gc_collected": stage.gc_debug.collected,
                "gc_uncollectable": stage.gc_debug.uncollectable
            })
        
        result["traceEvents"].append(stage_event)
        
        # Add stack frame markers as instant events
        for i, frame in enumerate(stage.stack_frames):
            frame_event = {
                "name": frame,
                "cat": "stack_frame",
                "ph": "i",  # Instant event
                "pid": pytorch_data.rank,
                "tid": stage.stage_id,
                "ts": stage.start_us * 1000,
                "args": {
                    "frame_index": i,
                    "stage_id": stage.stage_id
                }
            }
            result["traceEvents"].append(frame_event)
    
    # Write to JSON file
    with open(output_path, "w") as f:
        json.dump(result, f, indent=2)

if __name__ == "__main__":
    proto_to_json("trace.pb", "trace.json")