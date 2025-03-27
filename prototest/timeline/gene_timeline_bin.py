# generate_timeline_data.py
import random
import string
from typing import List
import timeline_pb2

def random_string(length: int = 10) -> str:
    return ''.join(random.choices(string.ascii_letters, k=length))

def generate_stack_frames(count: int = 3) -> List[timeline_pb2.StackFrame]:
    frames = []
    for _ in range(count):
        frame = timeline_pb2.StackFrame(
            address=random.randint(0, 2**64-1),
            so_name=f"lib{random_string(5)}.so"
        )
        frames.append(frame)
    return frames

def generate_stages(count: int = 5) -> List[timeline_pb2.Stage]:
    stages = []
    stage_types = [
        timeline_pb2.STAGE_DATALOADER,
        timeline_pb2.STAGE_FORWARD,
        timeline_pb2.STAGE_BACKWARD
    ]
    
    for i in range(count):
        stage = timeline_pb2.Stage(
            stage_id=i+1,
            stage_type=random.choice(stage_types),
            rank=random.randint(0, 3),
            step_id=random.randint(1, 100),
            comm=random_string(8),
            start_us=random.randint(0, 1000000),
            end_us=random.randint(1000000, 2000000),
            stack_frames=generate_stack_frames(random.randint(1, 5))
        )
        stages.append(stage)
    return stages

def save_to_binary(stages: List[timeline_pb2.Stage], filename: str):
    timeline = timeline_pb2.Timeline()
    timeline.stages.extend(stages)
    
    with open(filename, "wb") as f:
        f.write(timeline.SerializeToString())

if __name__ == "__main__":
    stages = generate_stages(10)
    save_to_binary(stages, "timeline_data.bin")
    print("Generated binary timeline data at timeline_data.bin")
