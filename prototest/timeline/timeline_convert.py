# binary_to_json.py
import json
from google.protobuf.json_format import MessageToJson
import timeline_pb2

def binary_to_json(input_file: str, output_file: str):
    timeline = timeline_pb2.Timeline()
    
    with open(input_file, "rb") as f:
        timeline.ParseFromString(f.read())
    
    json_data = MessageToJson(timeline)
    
    with open(output_file, "w") as f:
        f.write(json_data)

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("Usage: python binary_to_json.py <input.bin> <output.json>")
        sys.exit(1)
    
    binary_to_json(sys.argv[1], sys.argv[2])
    print(f"Converted {sys.argv[1]} to {sys.argv[2]}")
