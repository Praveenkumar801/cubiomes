file(REMOVE_RECURSE
  "libcubiomes_static.a"
  "libcubiomes_static.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/cubiomes_static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
