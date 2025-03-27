# convert_to_perfetto.py
from google.protobuf import json_format
from perfetto.trace_pb2 import Trace, TrackEvent
import timeline_pb2

def convert_to_perfetto(input_bin, output_pftrace):
    # 1. 读取原始数据
    timeline = timeline_pb2.Timeline()
    with open(input_bin, "rb") as f:
        timeline.ParseFromString(f.read())

    # 2. 创建Perfetto Trace
    trace = Trace()
    
    # 3. 添加轨道
    track = trace.packet.add()
    track.track_descriptor.uuid = 0x1234
    track.track_descriptor.name = "Training Timeline"

    # 4. 转换每个Stage
    for stage in timeline.stages:
        # 开始事件
        start = trace.packet.add()
        start.timestamp = stage.start_us * 1000  # μs → ns
        start.trusted_packet_sequence_id = 1
        
        event = start.track_event
        event.type = TrackEvent.TYPE_SLICE_BEGIN
        event.track_uuid = 0x1234
        event.name = f"{stage.comm} (Rank {stage.rank})"
        
        # 添加分类和自定义字段
        event.categories.append(stage.stage_type.name)
        event.debug_annotations.append(
            TrackEvent.DebugAnnotation(name="step_id", int_value=stage.step_id)
        )

        # 结束事件
        end = trace.packet.add()
        end.timestamp = stage.end_us * 1000
        end.trusted_packet_sequence_id = 1
        end.track_event.type = TrackEvent.TYPE_SLICE_END
        end.track_event.track_uuid = 0x1234

    # 5. 保存结果
    with open(output_pftrace, "wb") as f:
        f.write(trace.SerializeToString())

if __name__ == "__main__":
    import sys
    convert_to_perfetto("timeline.bin", "perfetto_trace.pftrace")
    print("Converted to perfetto_trace.pftrace")
