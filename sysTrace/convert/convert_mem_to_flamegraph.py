#!/usr/bin/env python3

import sys
import json
import os
import subprocess
from collections import defaultdict, deque
from concurrent.futures import ThreadPoolExecutor
from systrace_pb2 import ProcMem, StageType

class AccurateFlameGraphConverter:
    def __init__(self):
        # 按照要求的枚举顺序定义阶段名称
        self.stage_names = {
            StageType.STAGE_UNKNOWN: "UNKNOWN",
            StageType.STAGE_DATALOADER: "DATALOADER",
            StageType.STAGE_FORWARD: "FORWARD",
            StageType.STAGE_BACKWARD: "BACKWARD",
            StageType.STAGE_SYNCHRONIZATION: "SYNCHRONIZATION",
            # 注意：原proto中可能没有GC阶段，这里保持兼容性
            getattr(StageType, "STAGE_GC", 5): "GC"  # 处理可能的GC阶段
        }
        self.symbol_cache = {}
        self.so_path_cache = {}
        self.executor = ThreadPoolExecutor(max_workers=os.cpu_count() or 4)

    def convert(self, input_pb, output_json):
        """主转换函数"""
        proc_mem = self._load_proc_mem(input_pb)
        alloc_groups = self._analyze_allocations(proc_mem)
        self._precache_symbols(alloc_groups)
        
        trace_events = []
        global_timestamp = 0
        
        # 按照stage_id排序，同stage_id时按枚举顺序排序
        for (stage_type, stage_id), allocs in sorted(
            alloc_groups.items(),
            key=lambda x: (x[0][1], x[0][0])  # (stage_type, stage_id)
        ):
            stage_events, stage_duration = self._process_stage(
                allocs, proc_mem.pid, stage_id, stage_type, global_timestamp
            )
            trace_events.extend(stage_events)
            global_timestamp += stage_duration
        
        self._save_json(output_json, trace_events)
        self.executor.shutdown()

    def _process_stage(self, allocs, pid, stage_id, stage_type, base_timestamp):
        """处理单个stage的所有分配"""
        events = []
        stage_name = f"{stage_id}_{self.stage_names.get(stage_type, 'UNKNOWN')}"
        stage_duration = 0
        
        # Stage容器事件
        container_event = {
            "name": stage_name,
            "ph": "X",
            "ts": base_timestamp,
            "dur": 0,  # 临时值
            "pid": pid,
            "tid": pid,
            "args": {
                "stage_type": self.stage_names.get(stage_type, "UNKNOWN"),
                "stage_id": stage_id
            }
        }
        events.append(container_event)
        
        # 处理每个分配
        for alloc in allocs:
            alloc_events, alloc_duration = self._process_allocation(
                alloc, pid, stage_id, base_timestamp
            )
            events.extend(alloc_events)
            base_timestamp += alloc_duration
            stage_duration += alloc_duration
        
        container_event["dur"] = stage_duration
        return events, stage_duration

    def _process_allocation(self, alloc, pid, stage_id, base_timestamp):
        """处理单个内存分配事件"""
        events = []
        alloc_duration = alloc.mem_size
        
        # 构建调用栈树（从父到子）
        call_tree = {
            "name": "[root]",
            "duration": alloc_duration,
            "children": []
        }
        
        current_parent = call_tree
        for frame in alloc.stack_frames:
            so_name = os.path.basename(frame.so_name)
            symbol = self._resolve_symbol(so_name, frame.address)
            node = {
                "name": symbol,
                "duration": alloc_duration,  # 初始值
                "children": []
            }
            current_parent["children"].append(node)
            current_parent = node
        
        # 后序遍历调整duration（父=直接子节点之和）
        def adjust_durations(node):
            if node["children"]:
                node["duration"] = sum(adjust_durations(child) for child in node["children"])
            return node["duration"]
        adjust_durations(call_tree)
        
        # 生成事件（从最底层开始）
        stack = deque()
        stack.append((call_tree, base_timestamp, 0))  # (node, start_time, depth)
        
        while stack:
            node, start_time, depth = stack.popleft()
            
            events.append({
                "name": node["name"],
                "ph": "X",
                "ts": start_time,
                "dur": node["duration"],
                "pid": pid,
                "tid": pid,
                "args": {
                    "depth": depth,
                    "bytes": alloc.mem_size,
                    "alloc_ptr": f"0x{alloc.alloc_ptr:x}"
                }
            })
            
            # 保持调用顺序（第一个子节点在最左）
            for child in reversed(node["children"]):
                stack.appendleft((
                    child,
                    start_time,
                    depth + 1
                ))
        
        return events, alloc_duration

    def _load_proc_mem(self, path):
        """加载protobuf数据"""
        with open(path, "rb") as f:
            proc_mem = ProcMem()
            proc_mem.ParseFromString(f.read())
            return proc_mem

    def _analyze_allocations(self, proc_mem):
        """分析内存分配"""
        freed_ptrs = {free.alloc_ptr for free in proc_mem.mem_free_stacks}
        active_allocs = defaultdict(list)
        
        for alloc in proc_mem.mem_alloc_stacks:
            if alloc.alloc_ptr not in freed_ptrs:
                active_allocs[(alloc.stage_type, alloc.stage_id)].append(alloc)
        
        return active_allocs

    def _precache_symbols(self, alloc_groups):
        """预缓存符号"""
        unique_frames = set()
        for allocs in alloc_groups.values():
            for alloc in allocs:
                for frame in alloc.stack_frames:
                    so_name = os.path.basename(frame.so_name)
                    unique_frames.add((so_name, frame.address))
        
        # 并行解析
        list(self.executor.map(
            lambda args: self._resolve_symbol(*args),
            unique_frames
        ))

    def _resolve_symbol(self, so_name, address):
        """解析符号名"""
        cache_key = f"{so_name}:{address:x}"
        if cache_key in self.symbol_cache:
            return self.symbol_cache[cache_key]
        
        so_path = self._find_so_path(so_name)
        if not so_path:
            symbol = f"{so_name}@0x{address:x}"
            self.symbol_cache[cache_key] = symbol
            return symbol
        
        try:
            result = subprocess.run(
                ["addr2line", "-e", so_path, "-f", "-C", "-p", f"0x{address:x}"],
                capture_output=True, text=True, timeout=0.05
            )
            if result.returncode == 0:
                func_name = result.stdout.split(" at ")[0].split("(")[0].strip()
                symbol = f"{so_name}@{func_name}" if func_name else f"{so_name}@0x{address:x}"
            else:
                symbol = f"{so_name}@0x{address:x}"
        except:
            symbol = f"{so_name}@0x{address:x}"
        
        self.symbol_cache[cache_key] = symbol
        return symbol

    def _find_so_path(self, so_name):
        """查找so文件路径"""
        if so_name in self.so_path_cache:
            return self.so_path_cache[so_name]
        
        if os.path.isabs(so_name) and os.path.exists(so_name):
            self.so_path_cache[so_name] = so_name
            return so_name
        
        base_name = os.path.basename(so_name)
        search_paths = [
            "/usr/lib",
            "/usr/local/lib",
            "/lib",
            *os.getenv("LD_LIBRARY_PATH", "").split(":"),
            *os.getenv("PATH", "").split(":")
        ]
        
        for path in filter(os.path.isdir, search_paths):
            test_path = os.path.join(path, base_name)
            if os.path.exists(test_path):
                self.so_path_cache[so_name] = test_path
                return test_path
            
            if base_name.startswith("lib") and ".so" in base_name:
                lib_prefix = base_name.split(".so")[0]
                for ext in ["", ".1", ".2", ".3", ".4", ".5"]:
                    test_path = os.path.join(path, f"{lib_prefix}.so{ext}")
                    if os.path.exists(test_path):
                        self.so_path_cache[so_name] = test_path
                        return test_path
        
        self.so_path_cache[so_name] = None
        return None

    def _save_json(self, path, trace_events):
        """保存为Chrome Trace格式"""
        if os.path.isdir(path):
            input_name = os.path.splitext(os.path.basename(sys.argv[1]))[0]
            path = os.path.join(path, f"{input_name}_trace.json")
        
        with open(path, "w") as f:
            json.dump({
                "traceEvents": sorted(trace_events, key=lambda x: x["ts"]),
                "displayTimeUnit": "ns",
                "metadata": {
                    "format": "FlameGraph",
                    "stage_order": ["UNKNOWN", "DATALOADER", "FORWARD", "BACKWARD", "SYNCHRONIZATION", "GC"]
                }
            }, f, indent=2)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python proc_mem_converter.py input.pb output.json")
        sys.exit(1)
    
    AccurateFlameGraphConverter().convert(sys.argv[1], sys.argv[2])