#source: attr-merge-priv-spec-a.s
#source: attr-merge-priv-spec-d.s
#source: attr-merge-priv-spec-c.s
#as:
#ld: -r
#warning: .*use privileged spec version 1.11.0 but the output use version 1.10.0
#readelf: -A

Attribute Section: riscv
File Attributes
  Tag_RISCV_arch: [a-zA-Z0-9_\"].*
  Tag_RISCV_priv_spec: 1
  Tag_RISCV_priv_spec_minor: 11
