#!/usr/bin/python

import tomllib
import sys

def print_item(item, keybase=''):
    if isinstance(item, dict):
        prefix = f'{keybase}.' if keybase else ''
        for k, v in item.items():
            print_item(v, f'{prefix}{k}')
    elif isinstance(item, list):
        if isinstance(item[0], int) and keybase.endswith('palette'):
            itemstr = ', '.join(f'0x{x:06x}' for x in item)
        else:
            itemstr = ', '.join(str(x) for x in item)
        print(f'{keybase} = {itemstr}')
    else:
        if isinstance(item, int) and keybase.endswith('color'):
            print(f'{keybase} = 0x{item:06x}')
        else:
            print(f'{keybase} = {item}')

styles_custom = tomllib.load(sys.stdin.buffer)

print('# converted from TOML')
print_item(styles_custom)
