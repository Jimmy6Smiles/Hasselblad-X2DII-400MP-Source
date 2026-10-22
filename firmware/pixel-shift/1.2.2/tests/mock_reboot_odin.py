#!/usr/bin/python3
import json,os,runpy,sys
from pathlib import Path
if sys.argv[1:]==['-s','error','-p','active_error']:
 p=Path(os.environ['AUTO6_STATE']);s=json.loads(p.read_text());s['calls'].append(sys.argv[1:]);p.write_text(json.dumps(s))
 if s.get('error_query_failed'):sys.exit(1)
 print('active_error = '+('true' if s.get('active_error') else 'false'))
else:
 runpy.run_path(str(Path(__file__).with_name('mock_auto6.py')),run_name='__main__')
