import random
import string
from typing import List
import pytorch_pb2

def random_string(length: int = 8) -> str:
    return ''.join(random.choices(string.ascii_letters, k=length))

def generate_stack_frames(count: int = 3) -> List[pytorch_pb2.StackFrame]:
    frames = []
    for _ in range(count):
        frame = pytorch_pb2.StackFrame(
            address=random.randint(0, 2**64-1),
            so_name=f"lib{random_string()}.so"
        )
        frames.append(frame)
    return frames

def generate_stage(stage_id: int) -> pytorch_pb2.PytorchStage:
    stage_type = random.choice([
        pytorch_pb2.StageType.STAGE_DATALOADER,
        pytorch_pb2.StageType.STAGE_FORWARD,
        pytorch_pb2.StageType.STAGE_BACKWARD
    ])
    start_us = random.randint(0, 1000000)
    duration = random.randint(100, 10000)

    return pytorch_pb2.PytorchStage(
        stage_id=stage_id,
        stage_type=stage_type,
        rank=random.randint(0, 3),
        step_id=random.randint(0, 3),
        comm=random_string(4),
        start_us=start_us,
        end_us=start_us + duration,
        stack_frames=generate_stack_frames(random.randint(1, 5))
    )

def generate_pytorch(num_stages: int = 10) -> pytorch_pb2.Pytorch:
    pytorch = pytorch_pb2.Pytorch()
    for i in range(num_stages):
        pytorch.pytorch_stages.append(generate_stage(i))
    return pytorch

def save_to_binary(pytorch: pytorch_pb2.Pytorch, filename: str):
    with open(filename, 'wb') as f:
        f.write(pytorch.SerializeToString())

if __name__ == "__main__":
    pytorch = generate_pytorch(20)
    save_to_binary(pytorch, "pytorch_data.bin")
    print("Generated pytorch data saved to pytorch_data.bin")
