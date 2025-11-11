import random

numbers = []

# 生成若干顺序数
for i in range(1, 23):
    numbers.append(str(i))

# 生成10个0到100之间的随机数
# for i in range(2048):
#     numbers.append(str(random.randint(0, 10000)))

with open('input.txt', 'w') as f:
    f.write(' '.join(numbers))