"""Create the handheld library from existing CrossMix ROMs, art and apps."""
import json
import os
from pathlib import Path

root = Path(os.environ.get('BIGPICTURE_SDCARD', '/mnt/SDCARD'))
data = root / 'Data/BigPicture'
data.mkdir(parents=True, exist_ok=True)

def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        return {}

def art_for(rom, image_dirs, fallback):
    candidates = [rom.with_suffix('.png'), rom.with_suffix('.jpg')]
    for folder in image_dirs:
        candidates += [folder / (rom.stem + extension) for extension in ('.png', '.jpg', '.jpeg')]
        candidates += [folder / (rom.parent.name + extension) for extension in ('.png', '.jpg', '.jpeg')]
    return str(next((path for path in candidates if path.is_file()), fallback))

def background_for(rom, image_dirs, fallback):
    candidates = []
    for folder in [rom.parent] + image_dirs:
        candidates += [folder / (rom.stem + suffix) for suffix in ('.hero.jpg', '.hero.png', '.background.jpg', '.background.png')]
        candidates += [folder / 'backgrounds' / (rom.stem + ext) for ext in ('.jpg', '.png')]
    return str(next((path for path in candidates if path.is_file()), fallback))

systems = {}
apps = []
for cfg_file in sorted((root / 'Emus').glob('*/config.json')):
    cfg = read_json(cfg_file)
    key = cfg_file.parent.name
    if key in ('VIDEOS', 'MUSIC'):
        continue
    systems[key] = cfg
for cfg_file in sorted((root / 'Apps').glob('*/config.json')):
    cfg = read_json(cfg_file)
    if cfg_file.parent.name == 'BigPicture':
        continue
    launch = str(cfg.get('launch', 'launch.sh'))
    if '/' in launch or launch.startswith('.'):
        continue
    apps.append({'id': 'app:' + cfg_file.parent.name, 'title': cfg.get('label', cfg_file.parent.name).strip(),
                 'system': 'Aplicativos', 'rom': '', 'launcher': str(cfg_file.parent / launch),
                 'art': str(cfg_file.parent / cfg.get('icon', '')), 'kind': 'app'})

games = []
seen = set()
rom_roots = [root / 'Roms'] + sorted((root / 'Best').glob('*/Roms'))
for rom_root in rom_roots:
    if not rom_root.is_dir():
        continue
    for system_dir in sorted(rom_root.iterdir()):
        if not system_dir.is_dir() or system_dir.name not in systems:
            continue
        cfg = systems[system_dir.name]
        extensions = {'.' + ext.lower() for ext in cfg.get('extlist', '').split('|') if ext}
        extensions -= {'.png', '.jpg', '.jpeg', '.sh', '.launch', '.txt', '.cfg'}
        for rom in sorted(system_dir.rglob('*')):
            if not rom.is_file() or rom.suffix.lower() not in extensions or any(part.startswith('.') for part in rom.relative_to(system_dir).parts):
                continue
            identity = str(rom.relative_to(root))
            if identity in seen:
                continue
            seen.add(identity)
            art_dirs = [rom_root.parent / 'Imgs' / system_dir.name, rom_root.parent / 'Imgs', root / 'Imgs' / system_dir.name]
            fallback = root / 'Icons/Default/Emus' / (system_dir.name + '.png')
            games.append({'id': identity, 'title': rom.stem.replace('_', ' '),
                          'system': cfg.get('label', system_dir.name).strip(), 'system_id': system_dir.name,
                          'rom': str(rom), 'launcher': str(root / 'Emus' / system_dir.name / cfg.get('launch', 'default.sh')),
                          'art': art_for(rom, art_dirs, fallback), 'kind': 'game'})
            games[-1]['background'] = background_for(rom, art_dirs, Path(games[-1]['art']))

consoles = [{'id': key, 'title': cfg.get('label', key).strip(),
             'art': str(root / 'Icons/Default/Emus' / (key + '.png'))}
            for key, cfg in systems.items()]
collections = []
for folder in sorted((root / 'Best').glob('*')):
    if not folder.is_dir():
        continue
    prefix = str(folder.relative_to(root)) + '/'
    members = [game for game in games if game['id'].startswith(prefix)]
    if members:
        collections.append({'id': str(folder.relative_to(root)), 'title': folder.name,
                            'art': members[0]['art'], 'games': [game['id'] for game in members]})
catalog = {'games': games, 'apps': apps, 'consoles': consoles, 'collections': collections,
           'source': 'CrossMix SD card', 'version': 2}
temporary = data / 'catalog.json.tmp'
temporary.write_text(json.dumps(catalog, ensure_ascii=False, indent=2), encoding='utf-8')
temporary.replace(data / 'catalog.json')
print(f'Big Picture: {len(games)} jogos, {len(apps)} aplicativos, {len(consoles)} consoles e {len(collections)} colecoes.')
