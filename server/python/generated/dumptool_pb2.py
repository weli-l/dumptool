# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# NO CHECKED-IN PROTOBUF GENCODE
# source: dumptool.proto
# Protobuf Python Version: 5.29.0
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import runtime_version as _runtime_version
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
_runtime_version.ValidateProtobufRuntimeVersion(
    _runtime_version.Domain.PUBLIC,
    5,
    29,
    0,
    '',
    'dumptool.proto'
)
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x0e\x64umptool.proto\x12\x0b\x64umptool.v1\"\x83\x02\n\x0b\x44umpRequest\x12\x11\n\tdump_path\x18\x01 \x01(\t\x12\x0f\n\x07payload\x18\x02 \x01(\x0c\x12\x33\n\x06\x66ormat\x18\x03 \x01(\x0e\x32#.dumptool.v1.DumpRequest.DataFormat\x12\x38\n\x08metadata\x18\x04 \x03(\x0b\x32&.dumptool.v1.DumpRequest.MetadataEntry\x1a/\n\rMetadataEntry\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\t:\x02\x38\x01\"0\n\nDataFormat\x12\x08\n\x04JSON\x10\x00\x12\x0c\n\x08PROTOBUF\x10\x01\x12\n\n\x06\x42INARY\x10\x02\"0\n\x0c\x44umpResponse\x12\x0f\n\x07success\x18\x01 \x01(\x08\x12\x0f\n\x07message\x18\x02 \x01(\t2N\n\x0b\x44umpService\x12?\n\x08SendDump\x12\x18.dumptool.v1.DumpRequest\x1a\x19.dumptool.v1.DumpResponseb\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'dumptool_pb2', _globals)
if not _descriptor._USE_C_DESCRIPTORS:
  DESCRIPTOR._loaded_options = None
  _globals['_DUMPREQUEST_METADATAENTRY']._loaded_options = None
  _globals['_DUMPREQUEST_METADATAENTRY']._serialized_options = b'8\001'
  _globals['_DUMPREQUEST']._serialized_start=32
  _globals['_DUMPREQUEST']._serialized_end=291
  _globals['_DUMPREQUEST_METADATAENTRY']._serialized_start=194
  _globals['_DUMPREQUEST_METADATAENTRY']._serialized_end=241
  _globals['_DUMPREQUEST_DATAFORMAT']._serialized_start=243
  _globals['_DUMPREQUEST_DATAFORMAT']._serialized_end=291
  _globals['_DUMPRESPONSE']._serialized_start=293
  _globals['_DUMPRESPONSE']._serialized_end=341
  _globals['_DUMPSERVICE']._serialized_start=343
  _globals['_DUMPSERVICE']._serialized_end=421
# @@protoc_insertion_point(module_scope)
