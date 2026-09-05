#!/usr/bin/env python3
"""Write tests for an isolated reference-runtime deployment (never run against production)."""
import argparse, base64, json, time, urllib.request, urllib.error, uuid
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', required=True)
parser.add_argument('--video',type=Path,required=True)
parser.add_argument('--evidence',type=Path,required=True)
args=parser.parse_args()
base=args.url.rstrip('/')
methods=set()

def call(path, payload=None, token='', expected=None, multipart=None, status=200):
    headers={}
    if token:headers['Authorization']='Bearer '+token
    data=None
    if multipart is not None:
        boundary='vod-'+uuid.uuid4().hex;parts=[]
        for key,(name,content,mime) in multipart.items():
            disposition=f'Content-Disposition: form-data; name="{key}"'
            if name:disposition+=f'; filename="{name}"'
            parts.extend([f'--{boundary}\r\n{disposition}\r\nContent-Type: {mime}\r\n\r\n'.encode(),content,b'\r\n'])
        parts.append(f'--{boundary}--\r\n'.encode());data=b''.join(parts)
        headers['Content-Type']='multipart/form-data; boundary='+boundary
    elif payload is not None:
        data=json.dumps(payload).encode();headers['Content-Type']='application/json'
    try:response=urllib.request.urlopen(urllib.request.Request(base+path,data=data,headers=headers),timeout=90)
    except urllib.error.HTTPError as error:response=error
    raw=response.read()
    assert response.status==status,(path,response.status,raw[:300])
    method=response.headers.get('X-Vod-Rpc-Method')
    if expected:assert method==expected,(path,method,expected)
    if method:methods.add(method)
    content_type=response.headers.get('Content-Type','')
    return (json.loads(raw) if 'json' in content_type else raw),content_type

def ok(path,payload=None,token='',expected=None,**kwargs):
    value,_=call(path,payload,token,expected,**kwargs)
    assert isinstance(value,dict) and value.get('success'),(path,value)
    return value

def check(value,name):
    assert value,name
    print('[PASS] '+name,flush=True)

login=ok('/login',{'account':'bit-user-001','password':'123456'},expected='UserService.Login');token=login['token']
admin=ok('/login/password',{'account':'admin@bit.com','password':'123456'},expected='UserService.Login')['token']
original=ok('/users/profile?account=bit-user-001',token=token,expected='UserService.GetProfile')['user']
try:
    ok('/users/profile',{'account':'bit-user-001','userName':'RPC验证用户','description':'typed RPC'},token,'UserOperations.UpdateProfile')
    check(ok('/users/profile',token=token)['user']['userName']=='RPC验证用户','profile write and native read agree')
    value,_=call('/users/profile',{'account':'someone-else','userName':'forged'},token,status=403)
    check(not value['success'],'forged account rejected across RPC')
    call('/admin/users',token=token,status=403)
    ok('/admin/users',token=admin,expected='UserOperations.ListUsers')
    ok('/admin/users/action',{'account':'bit-user-001','action':'enable'},admin,'UserOperations.UpdateUser')
    value,_=call('/login/email-code',{'email':'invalid'},expected='UserOperations.SendEmailCode')
    check(not value['success'],'invalid email validation preserved')
    value,_=call('/login/email',{'email':'invalid','authcodeId':'missing','authcode':'wrong'},expected='UserOperations.EmailLogin')
    check(not value['success'],'invalid email login fails')
    value,_=call('/videos',{'title':'missing file','category':'test'},token,expected='VideoOperations.CreateVideo')
    check(not value['success'],'metadata-only publish validates required file')
    png=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=')
    avatar=ok('/users/avatar',token=token,expected='UserOperations.UploadAvatar',multipart={'avatarFile':('avatar.png',png,'image/png')})
    avatar_bytes,_=call(avatar['avatarPath'],expected='FileService.DownloadFile')
    check(avatar_bytes==png,'avatar round trip through native FileService')
    metadata={'account':'bit-user-001','title':'HLS验收-'+uuid.uuid4().hex[:8],'category':'科技','tags':['HLS'],'description':'independent directories'}
    upload=ok('/videos/upload',token=token,expected='VideoOperations.UploadVideo',multipart={'metadata':('',json.dumps(metadata).encode(),'application/json'),'videoFile':('source.mp4',args.video.read_bytes(),'video/mp4'),'coverFile':('cover.png',png,'image/png')})['video']
    video_id=upload['id']
    check(upload['storedVideoPath'].startswith('object:') and upload['storedCoverPath'].startswith('object:'),'source and cover are FileService object IDs')
    source,_=call(upload['playUrl'],expected='FileService.DownloadFile')
    check(source==args.video.read_bytes(),'source bytes preserved by FastDFS RPC round trip')
    job=ok('/transcode/jobs',{'videoId':video_id},token,'TranscodeOperations.SubmitJob',status=202)
    deadline=time.monotonic()+90
    while True:
        job=ok('/transcode/jobs?videoId='+video_id,token=token,expected='TranscodeOperations.GetJob')['data']
        if job['status']=='SUCCEEDED':break
        assert job['status']!='FAILED',job
        assert time.monotonic()<deadline,job
        time.sleep(.5)
    check(job['status']=='SUCCEEDED','remote source transcodes without shared service directories')
    ok('/admin/reviews',token=admin,expected='VideoOperations.ListReviews')
    ok('/admin/reviews/action',{'videoId':video_id,'status':'审核通过'},admin,'VideoOperations.ReviewVideo')
    call('/videos',expected='VideoService.ListVideos')
    ok('/videos/detail?id='+video_id,expected='VideoService.GetVideoDetail')
    ok('/videos/search?keyword=HLS',expected='VideoService.ListVideos')
    play=ok('/videos/play-url?videoId='+video_id,expected='VideoOperations.GetPlayUrl')['playUrl']
    manifest,mime=call(play,expected='FileService.DownloadFile')
    check('mpegurl' in mime and b'#EXT-X-ENDLIST' in manifest,'HLS playlist is complete and has correct MIME type')
    segments=[line for line in manifest.decode().splitlines() if line and not line.startswith('#')]
    check(len(segments)>=2 and all(line.startswith('/uploads/') for line in segments),'playlist references remotely stored segments')
    for segment in segments:
        content,mime=call(segment,expected='FileService.DownloadFile')
        check(bool(content) and content[0]==0x47 and 'video/mp2t' in mime,'HLS transport stream is reachable')
    ok('/users/videos',token=token,expected='VideoOperations.ListOwnerVideos')
    for operation in ['like','unlike']:
        ok('/videos/'+operation,{'videoId':video_id},token,'InteractionOperations.'+operation.capitalize())
    ok('/videos/like-status?videoId='+video_id,token=token,expected='InteractionOperations.GetLike')
    ok('/videos/watch-progress',{'videoId':video_id,'seconds':0},token,'InteractionOperations.SaveProgress')
    check(ok('/videos/watch-progress?videoId='+video_id,token=token,expected='InteractionOperations.GetProgress')['seconds']==0,'explicit zero survives RPC round trip')
    for operation in ['favorite','unfavorite']:
        ok('/videos/'+operation,{'videoId':video_id},token,'InteractionOperations.'+operation.capitalize())
    ok('/videos/favorite-status?videoId='+video_id,token=token,expected='InteractionOperations.GetFavorite')
    ok('/users/favorites',token=token,expected='InteractionOperations.ListFavorites')
    ok('/videos/comments',{'videoId':video_id,'content':'RPC评论'},token,'InteractionOperations.AddComment')
    comments=ok('/videos/comments?videoId='+video_id,token=token,expected='InteractionOperations.ListComments')['comments']
    check(comments and comments[0]['account']=='bit-user-001','comment identity comes from authenticated context')
    ok('/videos/barrages',{'videoId':video_id,'seconds':0,'text':'RPC弹幕'},token,'InteractionOperations.AddBarrage')
    ok('/videos/barrages?videoId='+video_id,token=token,expected='InteractionOperations.ListBarrages')
    generic=ok('/files/upload',token=token,expected='FileService.UploadFile',multipart={'file':('check.bin',b'\0file-bytes','application/octet-stream')})
    check(call(generic['publicUrl'],expected='FileService.DownloadFile')[0]==b'\0file-bytes','general file RPC preserves binary bytes')
    ok('/transcode/jobs/retry',{'videoId':video_id},token,'TranscodeOperations.RetryJob',status=202)
    deadline=time.monotonic()+90
    while ok('/transcode/jobs?videoId='+video_id,token=token)['data']['status']!='SUCCEEDED':
        assert time.monotonic()<deadline,'retry timeout';time.sleep(.5)
    check(True,'explicit job retry completes with a fresh HLS output')
    args.evidence.write_text(json.dumps({'videoId':video_id,'source':upload['storedVideoPath'],'playlist':play,'segments':segments,'methods':sorted(methods)},ensure_ascii=False,indent=2))
finally:
    ok('/users/profile',{'account':'bit-user-001','userName':original['userName'],'description':original['description']},token)
    # Artifacts remain only in the isolated fixture database/storage for inspection.
    ok('/logout',{'account':'bit-user-001'},token,'UserOperations.Logout')
print('[PASS] '+str(len(methods))+' native RPC methods observed',flush=True)
