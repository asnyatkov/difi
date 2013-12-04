@echo off
sc create CmdAsSystem type= own type= interact binPath= "cmd /c start cmd /k (cd c:\ ^& color ec ^& title ***** SYSTEM *****)"
net start CmdAsSystem
sc delete CmdAsSystem
