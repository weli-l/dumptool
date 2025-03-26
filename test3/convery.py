import json
import trace_pb2

def proto_to_json(input_path, output_path):
    # 读取Protobuf数据
    with open(input_path, "rb") as f:
        trace_data = trace_pb2.TraceData()
        trace_data.ParseFromString(f.read())
    
    # 构建JSON结构
    result = {
        "traceEvents": [],
        "stackFrames": {},
        "samples": [],
        "systemTraceEvents": "SystemTraceData",
        "otherData": {"version": "My Application v1.0"}
    }
    
    # 转换事件
    for event in trace_data.trace_events:
        json_event = {
            "name": event.name,
            "cat": event.cat,
            "ph": "X",
            "pid": event.pid,
            "tid": event.tid,
            "ts": event.ts,
            "dur": event.dur,
            "args": {
                "count": event.args.count,
                "thread.name": event.args.thread_name,
                "futex.top": list(event.args.futex_top),
                "event.type": event.args.event_type
            }
        }
        if event.track:
            json_event["track"] = event.track
        result["traceEvents"].append(json_event)
    
    # 转换堆栈帧
    for frame_id, frame in trace_data.stack_frames.items():
        json_frame = {
            "category": frame.category,
            "name": frame.name
        }
        if frame.parent:
            json_frame["parent"] = frame.parent
        result["stackFrames"][frame_id] = json_frame
    
    # 写入文件
    with open(output_path, "w") as f:
        json.dump(result, f, indent=2)

if __name__ == "__main__":
    proto_to_json("trace.pb", "trace.json")

