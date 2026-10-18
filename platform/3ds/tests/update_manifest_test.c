#include "update_manifest.h"
#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static json_t *make(const char *tag, bool pre, bool homebrew) {
  char url[512], name[100]; const char *v=tag[0]=='v'?tag+1:tag;
  snprintf(name,sizeof(name),"tmc-3ds-v%s.%s",v,homebrew?"3dsx":"cia");
  snprintf(url,sizeof(url),"https://github.com/" UPDATE_REPOSITORY "/releases/download/%s/%s",tag,name);
  return json_pack("{s:s,s:b,s:b,s:[{s:s,s:s,s:s,s:i}]}","tag_name",tag,"draft",0,"prerelease",pre,"assets",
    "name",name,"browser_download_url",url,"digest","sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","size",5000000);
}
static int parse(json_t *r,bool pre,bool hb,UpdateRelease *out) {
 char *s=json_dumps(r,JSON_COMPACT);int result=Update_ParseRelease(s,strlen(s),pre,hb,out);free(s);return result;
}
static json_t *make_notes(const char *tag, bool pre, const char *notes) {
  json_t *r = make(tag, pre, false);
  char body[256]; snprintf(body, sizeof(body), "intro\n<!-- tmc-notes -->\n%s\n<!-- /tmc-notes -->\nfooter", notes);
  json_object_set_new(r, "body", json_string(body));
  return r;
}
static int parse_list(json_t *r, bool betas, const char *installed, UpdateRelease *out) {
  char *s = json_dumps(r, JSON_COMPACT); int result = Update_ParseReleases(s, strlen(s), betas, false, installed, out); free(s); return result;
}
int main(int argc,char **argv) {
 assert(Update_IsNewer("v3.1","3.1-E1"));assert(Update_IsNewer("v3.1-E10","3.1-E9"));
 assert(Update_IsNewer("v3.10","3.9"));assert(!Update_IsNewer("v3.1-E99","3.1"));
 assert(!Update_IsNewer("v3.0","3.1-E1"));assert(!Update_IsNewer("v3.1-E1","3.1-E1"));
 assert(!Update_ValidVersion("3.1/../evil"));assert(!Update_ValidVersion("3..1"));
 assert(!Update_ValidVersion("3.1-E99999999"));assert(!Update_ValidVersion("3.1.2.3"));
 assert(Update_IsNewer("v0.1.0","0.1.0-beta"));assert(Update_IsNewer("v0.1.0","0.1.0-dev.3+abc1234"));
 assert(Update_IsNewer("v0.1.1-beta","0.1.0"));assert(!Update_IsNewer("v0.1.0-beta","0.1.0-beta"));
 assert(Update_IsNewer("v0.1.0-beta","0.1.0-dev.3.2+abc1234"));assert(!Update_IsNewer("v0.1.0-dev.5+abc1234","0.1.0-beta"));assert(!Update_IsNewer("v0.1.0","0.1.1-dev.1+abc1234"));
 assert(!Update_ValidVersion("0.1.0-dev/../x"));assert(!Update_ValidVersion("0.1.0-betax"));
 char lines[384][43];
 unsigned count=Update_FormatNotes("## Changelog\n\n- **Fixed** [world colors](https://example.com).\n![QR](https://example.com/qr.png)",lines,384);
 assert(count==3 && !strcmp(lines[0],"Changelog") && !lines[1][0] && !strcmp(lines[2],"- Fixed world colors."));
 count=Update_FormatNotes("> Beta\n<!-- tmc-notes -->\n## What's new\n\n- Faster map\n<!-- /tmc-notes -->\n## Changelog\n- x (abc)",lines,384);
 assert(count==3 && !strcmp(lines[0],"What's new") && !strcmp(lines[2],"- Faster map"));
 assert(Update_IsNewer("v1.3-E15","1.3-E14"));
 assert(!Update_IsNewer("v1.2","1.3-E14"));
 assert(Update_FormatNotes("<img src=\"https://example.com/qr.png\" />\n## Changelog",lines,384)==1 && !strcmp(lines[0],"Changelog"));
 assert(Update_FormatNotes("",lines,384)==1 && !strcmp(lines[0],"No changelog provided."));
 assert(Update_FormatNotes("ignored",lines,0)==0);
 char long_notes[12289];memset(long_notes,'x',12288);long_notes[12288]=0;
 count=Update_FormatNotes(long_notes,lines,384);assert(count==293);
 for(unsigned i=0;i<count;i++)assert(strlen(lines[i])<=42);
 assert(Update_FormatNotes(long_notes,lines,2)==2);
 UpdateRelease out;json_t *r=make("v3.1",false,false);assert(parse(r,false,false,&out)==1);
 assert(!strcmp(out.version,"v3.1")&&out.size==5000000);assert(parse(r,false,true,&out)==-1);
 json_t *a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"digest",json_string("sha256:abc"));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"browser_download_url",json_string("https://evil.example/zelda.cia"));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"size",json_integer(UPDATE_MAX_FILE+1));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);json_object_set_new(r,"draft",json_true());assert(parse(r,false,false,&out)==-1);json_decref(r);
 r=json_array();assert(parse(r,true,false,&out)==0);
 json_array_append_new(r,make("v3.2-E2",true,false));json_array_append_new(r,make("v3.2-E10",true,false));
 json_array_append_new(r,make("v3.3",false,false));assert(parse(r,true,false,&out)==1&&!strcmp(out.version,"v3.2-E10"));json_decref(r);
 r=make("v3.1",false,true);assert(parse(r,false,true,&out)==1);json_decref(r);
 {
   // The list endpoint: channels, the newest of betas and stable, and the notes in between.
   json_t *l=json_array();
   json_array_append_new(l,make_notes("v0.3.0",false,"- stable three"));
   json_array_append_new(l,make_notes("v0.2.1",true,"- beta two one"));
   json_array_append_new(l,make_notes("v0.2.0",false,"- stable two"));
   json_array_append_new(l,make_notes("v0.1.2",true,"- beta one two"));
   json_array_append_new(l,make_notes("v0.1.1",false,"- stable one one"));
   // BETAS on sees the stable release too (the newest of both), STABLE ignores the betas.
   assert(parse_list(l,true,"0.2.0",&out)==1&&!strcmp(out.version,"v0.3.0")&&!out.prerelease);
   assert(parse_list(l,false,"0.1.1",&out)==1&&!strcmp(out.version,"v0.3.0"));
   // A pre-release newer than every stable one is found with BETAS only.
   json_array_insert_new(l,0,make_notes("v0.4.0",true,"- beta four"));
   assert(parse_list(l,true,"0.3.0",&out)==1&&!strcmp(out.version,"v0.4.0")&&out.prerelease);
   assert(parse_list(l,false,"0.3.0",&out)==1&&!strcmp(out.version,"v0.3.0"));
   // Stable only: the notes of every stable release newer than the installed build, and of the betas
   // on the way to the newest stable (a stable lists everything since the previous one); a beta newer
   // than that stable (v0.4.0) or one the install already has (v0.1.2 for 0.2.0) stays out.
   assert(parse_list(l,false,"0.1.0",&out)==1&&strstr(out.notes,"stable three")&&strstr(out.notes,"stable two")&&
          strstr(out.notes,"stable one one")&&strstr(out.notes,"beta two one")&&strstr(out.notes,"beta one two")&&
          !strstr(out.notes,"beta four"));
   assert(strstr(out.notes,"stable three")<strstr(out.notes,"beta two one")&&
          strstr(out.notes,"beta two one")<strstr(out.notes,"stable two"));
   assert(parse_list(l,false,"0.2.0",&out)==1&&strstr(out.notes,"## v0.2.1 BETA")&&strstr(out.notes,"stable three")&&
          !strstr(out.notes,"beta one two")&&!strstr(out.notes,"beta four"));
   assert(parse_list(l,false,"0.3.0",&out)==0||!strstr(out.notes,"beta four"));
   // BETAS: every release in between, betas included; a build already on the newest gets none.
   assert(parse_list(l,true,"0.1.1",&out)==1&&strstr(out.notes,"beta four")&&strstr(out.notes,"beta two one")&&
          strstr(out.notes,"beta one two")&&strstr(out.notes,"stable two")&&!strstr(out.notes,"stable one one"));
   // The same number: a beta build is not offered its own pre-release, a dev build is.
   json_t *only=json_array();json_array_append_new(only,make_notes("v0.5.0",true,"- beta five"));
   assert(parse_list(only,true,"0.5.0-beta",&out)==1);assert(!Update_IsNewer("v0.5.0-beta","0.5.0-beta"));
   assert(Update_IsNewer("v0.5.0-beta","0.5.0-dev.3+abc1234"));
   assert(parse_list(only,false,"0.4.0",&out)==0);
   // An unusable old release in the list is skipped, not fatal; non-arrays are invalid.
   json_array_append_new(l,json_pack("{s:s,s:b,s:b}","tag_name","v0.0.9","draft",0,"prerelease",0));
   assert(parse_list(l,false,"0.1.0",&out)==1);
   assert(Update_ParseReleases("{}",2,true,false,"0.1.0",&out)==-1);
   json_decref(l);json_decref(only);
 }
 assert(Update_ParseRelease("{}garbage",9,false,false,&out)==-1);
 assert(Update_ParseRelease("{\"draft\":false,\"draft\":true}",28,false,false,&out)==-1);
 for(int i=1;i<argc;i++) {
   FILE *f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
   char *data=malloc(n);assert(fread(data,1,n,f)==(size_t)n);fclose(f);
   bool pre=i==2;int result=Update_ParseRelease(data,n,pre,false,&out);free(data);
   assert(result>=0);printf("Live %s response: %d %s\n",pre?"pre-release":"stable",result,out.version);
 }
 puts("PASS: version order, channels, assets, malformed JSON, digest/size/URL/draft guards, live GitHub responses");
}
