# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# NO CHECKED-IN PROTOBUF GENCODE
# source: trace.proto
# Protobuf Python Version: 5.28.1
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import runtime_version as _runtime_version
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
_runtime_version.ValidateProtobufRuntimeVersion(
    _runtime_version.Domain.PUBLIC,
    5,
    28,
    1,
    '',
    'trace.proto'
)
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x0btrace.proto\"H\n\nStackFrame\x12\n\n\x02id\x18\x01 \x01(\t\x12\x0c\n\x04name\x18\x02 \x01(\t\x12\x10\n\x08\x63\x61tegory\x18\x03 \x01(\t\x12\x0e\n\x06parent\x18\x04 \x01(\t\"V\n\tArguments\x12\r\n\x05\x63ount\x18\x01 \x01(\x05\x12\x13\n\x0bthread_name\x18\x02 \x01(\t\x12\x11\n\tfutex_top\x18\x03 \x03(\t\x12\x12\n\nevent_type\x18\x04 \x01(\t\"\xa1\x01\n\x05\x45vent\x12\x0c\n\x04name\x18\x01 \x01(\t\x12\x0b\n\x03\x63\x61t\x18\x02 \x01(\t\x12\x0b\n\x03pid\x18\x03 \x01(\r\x12\x0b\n\x03tid\x18\x04 \x01(\r\x12\n\n\x02ts\x18\x05 \x01(\x04\x12\x0b\n\x03\x64ur\x18\x06 \x01(\x04\x12\r\n\x05track\x18\x07 \x01(\t\x12\x18\n\x04\x61rgs\x18\x08 \x01(\x0b\x32\n.Arguments\x12!\n\x0cstack_frames\x18\t \x03(\x0b\x32\x0b.StackFrame\"\xae\x01\n\tTraceData\x12\x1c\n\x0ctrace_events\x18\x01 \x03(\x0b\x32\x06.Event\x12\x31\n\x0cstack_frames\x18\x02 \x03(\x0b\x32\x1b.TraceData.StackFramesEntry\x12\x0f\n\x07samples\x18\x03 \x03(\t\x1a?\n\x10StackFramesEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\x1a\n\x05value\x18\x02 \x01(\x0b\x32\x0b.StackFrame:\x02\x38\x01\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'trace_pb2', _globals)
if not _descriptor._USE_C_DESCRIPTORS:
  DESCRIPTOR._loaded_options = None
  _globals['_TRACEDATA_STACKFRAMESENTRY']._loaded_options = None
  _globals['_TRACEDATA_STACKFRAMESENTRY']._serialized_options = b'8\001'
  _globals['_STACKFRAME']._serialized_start=15
  _globals['_STACKFRAME']._serialized_end=87
  _globals['_ARGUMENTS']._serialized_start=89
  _globals['_ARGUMENTS']._serialized_end=175
  _globals['_EVENT']._serialized_start=178
  _globals['_EVENT']._serialized_end=339
  _globals['_TRACEDATA']._serialized_start=342
  _globals['_TRACEDATA']._serialized_end=516
  _globals['_TRACEDATA_STACKFRAMESENTRY']._serialized_start=453
  _globals['_TRACEDATA_STACKFRAMESENTRY']._serialized_end=516
# @@protoc_insertion_point(module_scope)
