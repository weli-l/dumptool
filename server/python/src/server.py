import grpc
from concurrent import futures
import time
from generated import dumptool_pb2
from generated import dumptool_pb2_grpc

class DumpService(dumptool_pb2_grpc.DumpServiceServicer):
    def SendDump(self, request, context):
        print(f"[Request] Path: {request.dump_path}")
        print(f"Format: {dumptool_pb2.DumpRequest.DataFormat.Name(request.format)}")
        print(f"Payload Size: {len(request.payload)} bytes")
        return dumptool_pb2.DumpResponse(
            success=True,
            message="Hello! Request processed"
        )

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    dumptool_pb2_grpc.add_DumpServiceServicer_to_server(DumpService(), server)
    server.add_insecure_port('[::]:50051')
    server.start()
    print("Server started on port 50051")
    try:
        while True:
            time.sleep(86400)
    except KeyboardInterrupt:
        server.stop(0)

if __name__ == '__main__':
    serve()