cd deb
md5deep -rl etc > DEBIAN/md5sums
md5deep -rl usr >> DEBIAN/md5sums
md5deep -rl var >> DEBIAN/md5sums
