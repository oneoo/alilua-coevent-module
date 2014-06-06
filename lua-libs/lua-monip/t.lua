local monip = require('monip')

print(monip.init('./17monipdb.dat'))

print(monip.find('8.8.8.8'))
print(monip.find('101.68.73.1'))
print(monip.find('220.181.112.1'))
