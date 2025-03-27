# generate_data.py
import random
import timeline_pb2

def generate_stage(stage_id):
    types = list(timeline_pb2.StageType.values())
    return timeline_pb2.Stage(
        stage_id=stage_id,
        stage_type=random.choice(types),
        rank=random.randint(0, 3),
        step_id=random.randint(1, 100),
        comm=f"process_{stage_id}",
        start_us=random.randint(0, 1_000_000),
        end_us=random.randint(1_000_000, 2_000_000),
        stack_frames=[
            timeline_pb2.StackFrame(
                address=random.getrandbits(64),
                so_name=f"lib_{stage_id}_{i}.so"
            ) for i in range(random.randint(1, 3))
        ]
    )

def save_timeline():
    timeline = timeline_pb2.Timeline()
    for i in range(10):
        timeline.stages.append(generate_stage(i+1))
    
    with open("timeline.bin", "wb") as f:
        f.write(timeline.SerializeToString())

if __name__ == "__main__":
    save_timeline()
    print("Generated timeline.bin")
