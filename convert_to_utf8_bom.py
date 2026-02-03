import os
import codecs

def convert_to_utf8_bom(file_path):
    # 支持的编码列表，按优先级尝试
    encodings = ['utf-8-sig', 'utf-8', 'gbk', 'gb2312', 'utf-16', 'ascii']
    content = None
    original_encoding = None

    for enc in encodings:
        try:
            with codecs.open(file_path, 'r', encoding=enc) as f:
                content = f.read()
                original_encoding = enc
                break
        except Exception:
            continue

    if content is not None:
        # 如果已经是 utf-8-sig 且内容没变，可以跳过（可选）
        # 但为了确保万无一失，直接重写
        try:
            with codecs.open(file_path, 'w', encoding='utf-8-sig') as f:
                f.write(content)
            print(f"Converted: {file_path} (Original: {original_encoding})")
            return True
        except Exception as e:
            print(f"Failed to write {file_path}: {e}")
            return False
    else:
        print(f"Could not read {file_path} with any supported encoding.")
        return False

def main():
    # 需要处理的文件后缀
    extensions = {'.h', '.cpp', '.c', '.vcxproj', '.sln', '.filters', '.user', '.rc', '.txt', '.md', '.py'}
    # 需要排除的目录
    exclude_dirs = {'.git', '.vs', 'x64', 'Debug', 'Release', 'ipch'}

    project_root = os.getcwd()
    
    for root, dirs, files in os.walk(project_root):
        # 排除目录
        dirs[:] = [d for d in dirs if d not in exclude_dirs]
        
        for file in files:
            ext = os.path.splitext(file)[1].lower()
            if ext in extensions:
                file_path = os.path.join(root, file)
                # 排除脚本自身，防止正在写入时出错
                if file == 'convert_to_utf8_bom.py':
                    continue
                convert_to_utf8_bom(file_path)

if __name__ == "__main__":
    main()
