import trace_pb2

def create_stack_frames():
    """创建并返回 StackFrame 对象列表（仅用于 Event.stack_frames）"""
    frames = []
    for i in range(1, 74):
        frame = trace_pb2.StackFrame()
        frame.id = str(i)
        frame.name = f"[unknown][u]" if i % 4 == 0 else f"func_{i}"
        frame.category = "func"
        if i > 1:
            frame.parent = str(i - 1)
        frames.append(frame)
    return frames

# 创建主 trace 对象
trace = trace_pb2.TraceData()

# ✅ 直接跳过 trace.stack_frames（不填充 map）
# trace.stack_frames.update({})  # 可加可不加，默认就是空的

# 创建 StackFrame 列表（仅用于 Event.stack_frames）
stack_frames = create_stack_frames()

# 添加事件
for rank in range(4):
    pid = 890002 + rank
    tid = 890003 + rank
    for step in ["dataloader", "forward", "backward"]:
        event = trace.trace_events.add()
        event.name = step
        event.cat = f"rank{rank}_step"
        event.pid = pid
        event.tid = tid
        event.ts = 1719976732221288
        event.dur = 5000000
        event.args.count = 19
        event.args.thread_name = "python3"

        # ✅ 直接引用 StackFrame 列表（前4个）
        event.stack_frames.extend(stack_frames[:4])

# 保存文件
with open("trace.pb", "wb") as f:
    f.write(trace.SerializeToString())
