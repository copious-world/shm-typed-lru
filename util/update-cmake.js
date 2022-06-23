const fs = require('fs')


let node_path = process.argv[2]
let cmakefiletxt = fs.readFileSync("./util/CMakeLists-abstract.txt").toString()
cmakefiletxt = cmakefiletxt.replace('$@REPLACE_NAN',node_path)
fs.writeFileSync("./CMakeLists.txt",cmakefiletxt)

