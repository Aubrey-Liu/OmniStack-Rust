Token被申请后

1. 如果token != thread_id当前必然无权限访问
2. 如果token == thread_id
    1. 如果没有其他人申请，return_tick = 0
    2. 如果有其他人申请，return_tick = 1/2
        1. return_tick = 2: 刚刚有人申请，发送一个主动放弃信息
        2. return_tick = 1: 刚刚有人申请，系统已经开始自动回收，不需要发送主动放弃信息

控制平面：
1. 0-1Tick：等待主动回收，修改return_tick=2
2. 1-2Tick：被动回收，先修改return_tick=1，然后修改token=申请者，然后告知申请者申请成功
3. 2Tick：修改return_tick = 0，此时才允许该Token被申请