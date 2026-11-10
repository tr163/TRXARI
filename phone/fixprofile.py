p="/root/.PHDGuidingV2"
lines=open(p).read().splitlines()
out=[]; sec=""; done=False
for ln in lines:
    if ln.startswith("["): sec=ln.strip()
    if sec=="[profile/1/scope]" and ln.startswith("LastMenuChoice=") and not done:
        out.append("LastMenuChoice=INDI Mount [LX200 OnStep]"); done=True; continue
    out.append(ln)
open(p,"w").write("\n".join(out)+"\n")
print("profile updated:", done)
