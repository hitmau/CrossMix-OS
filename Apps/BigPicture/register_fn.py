import json
from pathlib import Path
import sys

path = Path(sys.argv[1])
items = json.loads(path.read_text(encoding='utf-8-sig'))
if not any(item.get('launch') == 'com.crossmix.bigpicture.sh' for item in items):
    items.append({'name': 'Big Picture: standby', 'launch': 'com.crossmix.bigpicture.sh'})
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(items, ensure_ascii=False, indent=2), encoding='utf-8')
    temporary.replace(path)
