#!/usr/bin/env python3
"""Run real-service/fault tests only in a pre-provisioned isolated Linux VM fixture.
Requires .vod-test-fixture, review-config/{user,video,file,transcode,services,gateway}.json,
the fixture schema/vhost/etcd prefix described in docs/reference-service-boundaries.md.
All stopped processes are spawned by this script. Credentials are read locally and never printed.
"""
from pathlib import Path
import json,os,subprocess,time,urllib.request,urllib.error,signal,importlib.util
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture-root',type=Path,required=True)
ROOT=parser.parse_args().fixture_root.resolve()
if not (ROOT/'.vod-test-fixture').is_file():raise SystemExit('Missing .vod-test-fixture marker; refusing to run fault tests')
HOME=Path('/home/dev'); RUN=ROOT/'independent-runs';RUN.mkdir(exist_ok=True)
CONF=ROOT/'remaining-config';CONF.mkdir(exist_ok=True,mode=0o700)
discovery=json.loads((ROOT/'review-config/services.json').read_text())
assert discovery['registry']['prefix']=='/vod/review/01a07149' and discovery['redis']['port']==16379
assert json.loads((ROOT/'review-config/gateway.json').read_text())['server']['port']==12000
for service,port in [('file',12001),('user',12002),('video',12003),('transcode',12004)]:
 assert discovery[service+'_service']=='http://127.0.0.1:'+str(port)
client=[str(HOME/'.local/opt/mariadb/bin/mariadb'),'--no-defaults','--socket='+str(HOME/'.local/var/vod-mariadb-run/mysql.sock'),'-uroot','vod_boundaries_01a07149']
subprocess.run(client,input=(ROOT/'migrations/016_add_video_cover_path.sql').read_bytes(),check=True)
for name in ['user','video','file','transcode']:
 cfg=json.loads((ROOT/f'review-config/{name}.json').read_text());cwd=RUN/name;cwd.mkdir(exist_ok=True)
 assert cfg['database']['name']=='vod_boundaries_01a07149'
 assert cfg['redis']['port']==16379 and cfg['registry']['prefix']=='/vod/review/01a07149'
 assert cfg['rabbitmq']['virtual_host']=='/vod-boundaries-01a07149'
 assert cfg['server']['port'] in [12001,12002,12003,12004] and cfg['rpc']['port'] in [13001,13002,13003,13004]
 cfg.setdefault('log',{})['path']=str(cwd/'service.log')
 cfg.setdefault('transcode',{})['upload_root']=str(cwd/'uploads')
 cfg['transcode']['output_root']=str(cwd/'transcoded')
 cfg['transcode']['retry_delay_seconds']=3
 cfg['transcode']['lease_seconds']=6
 if name=='transcode':cfg['transcode']['ffmpeg_path']=str(HOME/'.local/opt/vod/ffmpeg-release/ffmpeg')
 dest=CONF/(name+'.json');dest.write_text(json.dumps(cfg));dest.chmod(0o600)
procs=[]
services={}
try:
 redis=next((HOME/'.local/opt').glob('redis-*/bin/redis-server'))
 procs.append(subprocess.Popen([str(redis),'--port','16379','--bind','127.0.0.1','--save','','--appendonly','no'],stdout=open(ROOT/'remaining-redis.log','w'),stderr=subprocess.STDOUT,start_new_session=True))
 time.sleep(1)
 def healthy(port):
  for i in range(60):
   if any(p.poll() is not None for p in procs):raise RuntimeError('review process exited')
   try:
    if urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=1).status==200:return
   except OSError:time.sleep(.3)
  raise RuntimeError('health timeout '+str(port))
 for name,port in [('file',12001),('user',12002),('video',12003),('transcode',12004)]:
  procs.append(subprocess.Popen([str(ROOT/f'build/svc_{name}/{name}_service'),str(CONF/(name+'.json'))],cwd=RUN/name,stdout=open(ROOT/f'remaining-{name}.log','w'),stderr=subprocess.STDOUT,start_new_session=True))
  services[name]=procs[-1]
  healthy(port)
 gateway_cwd=RUN/'gateway';gateway_cwd.mkdir(exist_ok=True)
 procs.append(subprocess.Popen([str(ROOT/'build/svc_gateway/api_gateway'),str(ROOT/'review-config/gateway.json'),str(ROOT/'review-config/services.json')],cwd=gateway_cwd,stdout=open(ROOT/'remaining-gateway.log','w'),stderr=subprocess.STDOUT,start_new_session=True))
 healthy(12000);time.sleep(2)
 ffmpeg=str(HOME/'.local/opt/vod/ffmpeg-release/ffmpeg');video=ROOT/'remaining-input.mp4'
 subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-y','-f','lavfi','-i','testsrc2=size=160x120:rate=15:duration=9','-c:v','libx264',str(video)],check=True)
 subprocess.run(['python3',str(ROOT/'test/integration/reference_runtime_test.py'),'--url','http://127.0.0.1:12000','--video',str(video),'--evidence',str(ROOT/'remaining-evidence.json')],check=True)
 assert not (RUN/'user/uploads').exists() and not (RUN/'video/uploads').exists()
 assert not list((RUN/'transcode/transcoded').glob('.work-*'))
 print('[PASS] independent service directories and private transcode cleanup',flush=True)
 # Fault injection operates only on PIDs created by this review, each in its own process group.
 spec=importlib.util.spec_from_file_location('smoke',ROOT/'tools/smoke_api.py');smoke=importlib.util.module_from_spec(spec);spec.loader.exec_module(smoke)
 base='http://127.0.0.1:12000'
 token=smoke.request_json(base,'POST','/login',{'account':'bit-user-001','password':'123456'})['token']
 def stop_service(name,crash=False):
  process=services[name];assert process in procs
  if crash:os.killpg(process.pid,signal.SIGKILL)
  else:process.terminate()
  process.wait(timeout=10);procs.remove(process)
 def start_service(name):
  process=subprocess.Popen([str(ROOT/f'build/svc_{name}/{name}_service'),str(CONF/(name+'.json'))],cwd=RUN/name,stdout=open(ROOT/f'fault-{name}.log','a'),stderr=subprocess.STDOUT,start_new_session=True)
  procs.append(process);services[name]=process;healthy({'file':12001,'transcode':12004}[name])
 def upload_source(label):
  metadata=json.dumps({'account':'bit-user-001','title':'fault-'+label+'-'+str(time.time_ns()),'category':'科技'})
  result=smoke.multipart_request_json(base,'/videos/upload',{'metadata':metadata},{'videoFile':('source.mp4',video.read_bytes(),'video/mp4')},token)
  assert result.get('success'),result
  return result['video']['id']
 def wait_job(id,predicate,timeout=45):
  deadline=time.monotonic()+timeout
  while time.monotonic()<deadline:
   result=smoke.request_json(base,'GET','/transcode/jobs?videoId='+id,token=token)['data']
   if predicate(result):return result
   time.sleep(.1)
  raise AssertionError(('job timeout',result))
 stop_service('transcode')
 id=upload_source('file-outage')
 stop_service('file');start_service('transcode')
 first=wait_job(id,lambda j:j['attempts']>=1 and j['status']=='PENDING')
 start_service('file')
 recovered=wait_job(id,lambda j:j['status']=='SUCCEEDED')
 assert recovered['attempts']>=2
 print('[PASS] FileService outage triggers delayed retry and then succeeds',flush=True)
 stop_service('transcode')
 id=upload_source('worker-crash')
 transcode_cfg=json.loads((CONF/'transcode.json').read_text());real_ffmpeg=transcode_cfg['transcode']['ffmpeg_path']
 wrapper=RUN/'transcode/slow-ffmpeg.sh';wrapper.write_text('#!/bin/sh\nsleep 5\nexec '+real_ffmpeg+' "$@"\n');wrapper.chmod(0o700)
 transcode_cfg['transcode']['ffmpeg_path']=str(wrapper);(CONF/'transcode.json').write_text(json.dumps(transcode_cfg))
 start_service('transcode');wait_job(id,lambda j:j['status']=='RUNNING')
 stop_service('transcode',crash=True)
 transcode_cfg['transcode']['ffmpeg_path']=real_ffmpeg;(CONF/'transcode.json').write_text(json.dumps(transcode_cfg))
 start_service('transcode')
 recovered=wait_job(id,lambda j:j['status']=='SUCCEEDED')
 assert recovered['attempts']>=2
 assert not list((RUN/'transcode/transcoded').glob('.work-*'))
 print('[PASS] killed Worker lease expires, job resumes, abandoned workspace is removed',flush=True)
 # Cache generation race and duplicate invalidation use the same isolated Redis.
 env=dict(os.environ,VIDEO_TEST_REDIS_PORT='16379')
 subprocess.run(['make','-C',str(ROOT/'test/integration'),'profile-cache'],env=env,check=True)
 # A direct internal HTTP call simulates a request already authenticated before Redis failed.
 def sql(statement):
  return subprocess.run(client+['-N','-B'],input=statement,text=True,check=True,capture_output=True).stdout.strip()

 # Force the second statement in the profile transaction to fail, proving the first rolls back.
 original_name=sql("SELECT user_name FROM users WHERE account='bit-user-001';")
 sql("DROP TRIGGER IF EXISTS review_reject_outbox;")
 sql("CREATE TRIGGER review_reject_outbox BEFORE INSERT ON outbox_events FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected outbox insert failure';")
 try:
  payload=json.dumps({'account':'bit-user-001','userName':'must roll back','description':'injected failure'}).encode()
  request=urllib.request.Request('http://127.0.0.1:12002/users/profile',data=payload,headers={'Content-Type':'application/json','X-Gateway-Verified':'1','X-Authenticated-Account':'bit-user-001'})
  try:urllib.request.urlopen(request,timeout=15);raise AssertionError('outbox failure unexpectedly succeeded')
  except urllib.error.HTTPError as error:assert error.code==500
  assert sql("SELECT user_name FROM users WHERE account='bit-user-001';")==original_name
 finally:sql("DROP TRIGGER review_reject_outbox;")
 print('[PASS] profile mutation rolls back when the transactional outbox insert fails',flush=True)
 redis_process=procs[0];redis_process.terminate();redis_process.wait(timeout=5);procs.remove(redis_process)
 before=sql("SELECT COALESCE(MAX(id),0) FROM outbox_events;")
 payload=json.dumps({'account':'bit-user-001','userName':'故障恢复测试','description':'durable cache event'}).encode()
 request=urllib.request.Request('http://127.0.0.1:12002/users/profile',data=payload,headers={'Content-Type':'application/json','X-Gateway-Verified':'1','X-Authenticated-Account':'bit-user-001'})
 with urllib.request.urlopen(request,timeout=15) as response:assert json.load(response)['success']
 event_id=sql("SELECT event_id FROM outbox_events WHERE id>"+before+" AND aggregate_id='bit-user-001' ORDER BY id DESC LIMIT 1;")
 assert event_id
 time.sleep(5)
 assert sql("SELECT COUNT(*) FROM consumed_events WHERE consumer_name='user_profile_cache' AND event_id='"+event_id+"';")=='0'
 redis_process=subprocess.Popen([str(redis),'--port','16379','--bind','127.0.0.1','--save','','--appendonly','no'],stdout=open(ROOT/'fault-redis.log','w'),stderr=subprocess.STDOUT,start_new_session=True)
 procs.append(redis_process)
 deadline=time.monotonic()+20
 while time.monotonic()<deadline:
  if sql("SELECT COUNT(*) FROM consumed_events WHERE consumer_name='user_profile_cache' AND event_id='"+event_id+"';")=='1':break
  time.sleep(.2)
 else:raise AssertionError('cache event did not recover')
 print('[PASS] Redis outage leaves durable event pending; consumer retries beyond its poison-message limit and recovers',flush=True)
 token=smoke.request_json(base,'POST','/login',{'account':'bit-user-001','password':'123456'})['token']
 profile=smoke.request_json(base,'GET','/users/profile',token=token)
 assert profile['user']['userName']=='故障恢复测试'
 print('[PASS] recovered cache returns authoritative database profile',flush=True)
 # Stop business workers before legacy DB concurrency tests create their own synthetic jobs.
 for process in list(procs):
  if process is redis_process:continue
  process.terminate();process.wait(timeout=10);procs.remove(process)
 db=json.loads((CONF/'user.json').read_text())['database']
 env=dict(os.environ,VIDEO_TEST_REDIS_PORT='16379',VIDEO_TEST_MYSQL_HOST=db['host'],VIDEO_TEST_MYSQL_PORT=str(db['port']),VIDEO_TEST_MYSQL_USER=db['user'],VIDEO_TEST_MYSQL_PASSWORD=db['password'],VIDEO_TEST_MYSQL_DATABASE=db['name'])
 subprocess.run(['make','integration-redis','integration-mysql','integration-gateway-auth'],cwd=ROOT,env=env,check=True)


finally:
 for process in reversed(procs):
  if process.poll() is None:
   process.terminate()
   try:process.wait(timeout=10)
   except subprocess.TimeoutExpired:process.kill();process.wait()
 print('Stopped review processes',flush=True)
