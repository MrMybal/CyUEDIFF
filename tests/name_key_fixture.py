"""Expected key contract, independent of Core and journal implementation."""
import json

def name_key_id(base, number=0):
    display = base + ('_' + str(number - 1) if number else '')
    return 'ue-name/1:' + json.dumps([display, {'': {'base': base, 'number': number}}], ensure_ascii=False, sort_keys=True, separators=(',', ':'))

def key_token(value):
    return value.replace('~', '~0').replace('/', '~1')
