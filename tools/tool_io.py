import json
import os
import sys


def load_request():
    args = sys.argv[1:]
    if "--input-dir" in args and "--output-dir" in args:
        input_dir = args[args.index("--input-dir") + 1]
        output_dir = args[args.index("--output-dir") + 1]

        params_path = os.path.join(input_dir, "params.json")
        params = {}
        if os.path.exists(params_path):
            with open(params_path, "r", encoding="utf-8") as handle:
                params = json.load(handle)

        input_file = os.path.join(input_dir, "input.wav")
        if not os.path.exists(input_file):
            input_file = None

        return params, input_file, output_dir

    payload = sys.stdin.read().strip()
    request = json.loads(payload) if payload else {}
    return (
        request.get("params", {}),
        request.get("input_file"),
        request.get("output_dir", "/tmp"),
    )


def emit_result(output_dir, result):
    os.makedirs(output_dir, exist_ok=True)

    result_path = os.path.join(output_dir, "result.json")
    with open(result_path, "w", encoding="utf-8") as handle:
        json.dump(result, handle)

    print(json.dumps(result))
