# The pinned sibling checkout (~/DOSGL by default, config.mk): DOSGL carries
# DOS-GL, SDL3 and its patches, and the vendored harness. The build refuses a
# DOSGL older than this commit, and says when DOSGL has moved past it. Keep
# it at DOSGL main's latest: when DOSGL moves, `make regress` (and the full
# `make suite` on the cards before a release), then `make pin`.
DOSGL_PIN := 6fd09694928d9fccf9ab5305e2419f174178b29f
