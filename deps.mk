# The pinned sibling checkouts (config.mk): DOSGL (~/DOSGL) carries DOS-GL,
# SDL3 and its patches, and the vendored harness; dgk (~/dgk) is the kit,
# split from this repository's kit/. The build refuses either one older than
# its pin, and says when one has moved past it. Keep them at their latest:
# when one moves, `make regress` (and the full `make suite` on the cards
# before a release), then `make pin`.
DOSGL_PIN := 6fd09694928d9fccf9ab5305e2419f174178b29f
DGK_PIN   := 17ce423bfef3425c3281542dac69040fd46aabe2
