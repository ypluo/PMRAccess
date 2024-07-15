import sys

if len(sys.argv) == 1:
    print("Usage:")
    print("\t", sys.argv[0], " filename")
    exit(-1)
else:
    fname = sys.argv[1]

params = []
latmin = []
lat50 = []
lat90 = []
lat99 = []
lat999 = []
lat9999 = []

fin = open(fname, 'r')
line = fin.readline()
while len(line) > 0:
    params.append(int(line))
    line = fin.readline()
    latmin.append(line.replace("min  latency:", "").strip('\n'))

    line = fin.readline()
    lat50.append(float(line.replace("50% latency:", "").strip('\n')))

    line = fin.readline()
    lat90.append(float(line.replace("90% latency:", "").strip('\n')))

    line = fin.readline()
    lat99.append(float(line.replace("99% latency:", "").strip('\n')))

    line = fin.readline()
    lat999.append(float(line.replace("999% latency:", "").strip('\n')))

    line = fin.readline()
    lat9999.append(float(line.replace("9999% latency:", "").strip('\n')))

    line = fin.readline()
fin.close()

for i in range(len(params)):
    print(latmin[i], lat50[i], lat90[i], lat99[i], lat999[i], lat9999[i])