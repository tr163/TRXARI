p="/root/.PHDGuidingV2"
lines=open(p).read().splitlines()
out=[]; sec=""; changed=[]
for ln in lines:
    if ln.startswith("["): sec=ln.strip()
    if sec=="[profile/1/indi]" and ln.startswith("INDImount="):
        out.append("INDImount=LX200 OnStep"); changed.append("INDImount"); continue
    out.append(ln)
open(p,"w").write("\n".join(out)+"\n")
print("changed:", changed)
