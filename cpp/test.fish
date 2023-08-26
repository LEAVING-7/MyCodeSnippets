#!/usr/bin/fish

set max_attempts 1  # 设置最大尝试次数
set attempt 0

echo "开始尝试运行程序..."
while [ $attempt -le $max_attempts ]
    echo "尝试运行程序，第 $attempt 次..."
    # ./a.out  

    echo "(退出码: $status)..."
    if test $attempt -eq $max_attempts
        echo "达到最大尝试次数，无法继续尝试。"
        break
    else
        echo "等待片刻后进行下一次尝试..."
        set attempt (math $attempt + 1)
    end
end