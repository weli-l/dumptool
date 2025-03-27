import random
import string
from typing import List
import pytorch_pb2

# 配置参数
TOTAL_STAGES = 100_000     # 生成阶段总数
MAX_STACK_FRAMES = 100    # 每个阶段最大堆栈帧数
TIME_RANGE_US = 1_000_000_000  # 时间轴范围（1秒=1,000,000微秒，这里设为1000秒）

def random_string(length: int = 16) -> str:
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def generate_stack_frames(count: int) -> List[pytorch_pb2.StackFrame]:
    return [ 
        pytorch_pb2.StackFrame(
            address=random.getrandbits(64),
            so_name=f"lib{random_string(12)}.so"
        ) for _ in range(count)
    ]

def generate_stage(stage_id: int) -> pytorch_pb2.PytorchStage:
    start_us = random.randint(0, TIME_RANGE_US)
    return pytorch_pb2.PytorchStage(
        stage_id=stage_id,
        stage_type=random.choice([
            pytorch_pb2.StageType.STAGE_DATALOADER,
            pytorch_pb2.StageType.STAGE_FORWARD,
            pytorch_pb2.StageType.STAGE_BACKWARD
        ]),
        rank=random.randint(0, 1023),          # 支持最多1024个rank
        step_id=random.randint(0, 10_000),     # 支持更大step范围
        comm=random_string(8),                 # 更长的通信标识符
        start_us=start_us,
        end_us=start_us + random.randint(1, 1_000_000),  # 最长1秒的持续时间
        stack_frames=generate_stack_frames(random.randint(1, MAX_STACK_FRAMES))
    )

def generate_pytorch(num_stages: int) -> pytorch_pb2.Pytorch:
    pytorch = pytorch_pb2.Pytorch()
    pytorch.pytorch_stages.extend(
        generate_stage(i) for i in range(num_stages)
    )
    return pytorch

def save_to_binary(pytorch: pytorch_pb2.Pytorch, filename: str):
    with open(filename, 'wb') as f:
        f.write(pytorch.SerializeToString())

if __name__ == "__main__":
    pytorch = generate_pytorch(TOTAL_STAGES)
    save_to_binary(pytorch, "pytorch_large_data.bin")
    print(f"Generated {TOTAL_STAGES} stages, output size: {len(pytorch.SerializeToString()) >> 20}MB")
