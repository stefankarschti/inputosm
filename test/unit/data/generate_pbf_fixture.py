#!/usr/bin/env python3
"""Create the checked-in PBF fixtures with osmium. Tests do not run this script."""

from datetime import datetime, timedelta, timezone
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
NODE_COUNT = 17005


def node_id(index):
    return index + 1 if index < 17000 else 5_000_000_000 + index


def metadata(index):
    timestamp = datetime(2020, 1, 2, tzinfo=timezone.utc) + timedelta(seconds=index)
    return dict(version=str(index % 7 + 1), timestamp=timestamp.strftime('%Y-%m-%dT%H:%M:%SZ'),
                changeset=str(10000 + index % 17), uid=str(index % 4 + 1), user=f'reader{index % 4 + 1}')


def add_tag(entity, key, value):
    ET.SubElement(entity, 'tag', k=key, v=value)


def document(count):
    root = ET.Element('osm', version='0.6', generator='inputosm fixture')
    ET.SubElement(root, 'bounds', minlat='-45', minlon='119', maxlat='-44', maxlon='120')
    for index in range(count):
        lat = -450000000 + index % 9000 * 1000
        lon = 1200000000 - index % 10000 * 1000
        node = ET.SubElement(root, 'node', id=str(node_id(index)),
                             lat=f'{lat / 10000000:.7f}', lon=f'{lon / 10000000:.7f}', **metadata(index))
        if index % 3 == 0:
            add_tag(node, 'name', f'node {index} – café')
            add_tag(node, 'source', 'fixture')
            add_tag(node, 'empty', '')
        if index == 0:
            for tag in range(270):
                add_tag(node, f'key{tag}', f'value{tag}')
        if index == count - 1:
            add_tag(node, 'long', 'x' * 255)
    for index in range(12):
        way = ET.SubElement(root, 'way', id=str(10000000000 + index), **metadata(index + 100))
        refs = [node_id(i) for i in range(count)] if index == 0 else [node_id(count - 1), 1, 2, 1]
        for ref in refs:
            ET.SubElement(way, 'nd', ref=str(ref))
        add_tag(way, 'route', 'ferry' if index % 4 == 0 else 'road')
        add_tag(way, 'name', f'way {index}')
        add_tag(way, 'empty', '')
    for index in range(4):
        relation = ET.SubElement(root, 'relation', id=str(20000000000 + index), **metadata(index + 200))
        ET.SubElement(relation, 'member', type='node', ref='1', role='stop')
        ET.SubElement(relation, 'member', type='way', ref=str(10000000000 + index), role='')
        ET.SubElement(relation, 'member', type='relation', ref=str(20000000000 + (index + 1) % 4), role='café')
        add_tag(relation, 'type', 'route')
        add_tag(relation, 'name', f'relation {index}')
    return ET.ElementTree(root)


with tempfile.TemporaryDirectory() as temporary:
    source = Path(temporary) / 'fixture.osm'
    for count, name, options in [
        (NODE_COUNT, 'comprehensive.osm.pbf', 'pbf,pbf_compression=zlib'),
        (7, 'ordinary.osm.pbf', 'pbf,pbf_dense_nodes=false,pbf_compression=none'),
    ]:
        document(count).write(source, encoding='utf-8', xml_declaration=True)
        subprocess.run(['osmium', 'cat', str(source), '-f', options, '--generator', 'inputosm fixture',
                        '-o', str(ROOT / name), '--overwrite'], check=True)
