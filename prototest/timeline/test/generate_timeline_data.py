# generate_timeline_data.py
import random
import string
from typing import List
import timeline_pb2

def random_string(length: int = 8) -> str:
    return ''.join(random.choices(string.ascii_letters, k=length))

def generate_stack_frames(count: int = 3) -> List[timeline_pb2.StackFrame]:
    frames = []
    for _ in range(count):
        frame = timeline_pb2.StackFrame(
            address=random.randint(0, 2**64-1),
            so_name=f"lib{random_string()}.so"
        )
        frames.append(frame)
    return frames

def generate_stage(stage_id: int) -> timeline_pb2.Stage:
    stage_type = random.choice(list(timeline_pb2.StageType.values()))
    start_us = random.randint(0, 1000000)
    duration = random.randint(100, 10000)
    
    return timeline_pb2.Stage(
        stage_id=stage_id,
        stage_type=stage_type,
        rank=random.randint(0, 3),
        step_id=random.randint(0, 3),
        comm=random_string(4),
        start_us=start_us,
        end_us=start_us + duration,
        stack_frames=generate_stack_frames(random.randint(1, 5))
    )

def generate_timeline(num_stages: int = 10) -> timeline_pb2.Timeline:
    timeline = timeline_pb2.Timeline()
    for i in range(num_stages):
        timeline.stages.append(generate_stage(i))
    return timeline

def save_to_binary(timeline: timeline_pb2.Timeline, filename: str):
    with open(filename, 'wb') as f:
        f.write(timeline.SerializeToString())

if __name__ == "__main__":
    timeline = generate_timeline(20)  # Generate 20 stages
    save_to_binary(timeline, "timeline_data.bin")
    print("Generated timeline data saved to timeline_data.bin")
