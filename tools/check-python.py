import ast
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
files = sorted(root.joinpath('tools').glob('*.py'))
for file in files:
    ast.parse(file.read_text(encoding='utf-8'), filename=str(file))
    print('PYTHON_OK ' + file.name)
print('PYTHON_SYNTAX_OK')
