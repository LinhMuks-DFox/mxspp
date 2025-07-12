import json
import argparse
import pathlib
import sys
import subprocess

# .devcontainer.json 的基础配置内容
BASE_CONTENT = {
    "name": "mxspp-dev",
    "dockerComposeFile": "../docker-dev/docker-compose.yml",
    "service": "mxspp-dev",
    "workspaceFolder": "/workspace",
    "runServices": [
        "mxspp-dev"
    ],
    "remoteUser": "root",
    # 为扩展预留结构
    "customizations": {
        "vscode": {
            "extensions": []
        }
    }
}

# 默认安装的 VS Code 扩展ID集合
# 使用集合可以自动处理重复项
DEFAULT_EXTENSIONS = {
    "ms-python.autopep8",
    "ms-python.debugpy",
    "ms-python.python",
    "ms-python.vscode-pylance",
    "llvm-vs-code-extensions.vscode-clangd",
    "xaver.clang-format",
}


def get_host_extensions():
    """
    获取宿主机上当前安装的 VS Code 扩展列表。
    """
    try:
        # 执行 'code --list-extensions' 命令
        # check=True: 如果命令返回非零退出码，会抛出 CalledProcessError
        result = subprocess.run(
            ['code', '--list-extensions'],
            capture_output=True,
            text=True,
            check=True,
            encoding='utf-8'
        )
        # 按行分割输出，并过滤掉空行
        extensions = set(filter(None, result.stdout.splitlines()))
        print(f"Found {len(extensions)} extensions on the host machine.")
        return extensions
    except FileNotFoundError:
        print(
            "Error: 'code' command not found. Make sure VS Code is installed "
            "and 'code' is in your system's PATH.",
            file=sys.stderr
        )
        return set()
    except subprocess.CalledProcessError as e:
        print(f"Error executing 'code --list-extensions': {e}", file=sys.stderr)
        print(f"Stderr: {e.stderr}", file=sys.stderr)
        return set()


def parse_args():
    """
    解析命令行参数
    """
    parser = argparse.ArgumentParser(
        description="Create a personal .devcontainer.json file."
    )
    parser.add_argument(
        "-e", "--extensions",
        nargs='+',  # 接受一个或多个参数值
        metavar="PUBLISHER.EXTENSION_NAME",
        help="Space-separated list of additional VS Code extensions to install."
    )
    parser.add_argument(
        "-o", "--output",
        type=pathlib.Path,
        default=pathlib.Path("./.devcontainer.json"),
        help="Output file path for the devcontainer.json. Default is './.devcontainer.json'."
    )
    parser.add_argument(
        "--no-default",
        action="store_true",
        help="Do not include the default set of extensions."
    )
    parser.add_argument(
        "--use-current-vs-extension-list",
        action="store_true",
        help="Include all extensions currently installed on the host machine."
    )
    return parser.parse_args()


def main():
    """
    主函数，用于生成 devcontainer 文件
    """
    args = parse_args()

    final_extensions = set()

    # 如果用户没有指定 --no-default，则使用默认扩展列表
    if not args.no_default:
        final_extensions.update(DEFAULT_EXTENSIONS)

    # 如果用户指定了 --use-current-vs-extension-list，则获取宿主机扩展
    if args.use_current_vs_extension_list:
        host_extensions = get_host_extensions()
        final_extensions.update(host_extensions)

    # 添加用户通过命令行传入的额外扩展
    if args.extensions:
        final_extensions.update(args.extensions)

    # 准备最终的JSON配置
    config = BASE_CONTENT.copy()
    # 将集合转换为排序后的列表，以保证每次生成的文件内容顺序一致
    config["customizations"]["vscode"]["extensions"] = sorted(list(final_extensions))

    # 将配置写入目标文件
    output_file = args.output
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            # 使用 indent=4 来格式化输出的 JSON 文件
            json.dump(config, f, indent=4, ensure_ascii=False)
        print(f"Successfully created '{output_file}' with {len(final_extensions)} extensions.")
    except IOError as e:
        print(f"Error writing to file {output_file}: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
