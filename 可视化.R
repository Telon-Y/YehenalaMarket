library(showtext)
showtext_auto()

# 设置工作目录
# setwd("CSV所在目录")

prices     <- read.csv("prices.csv",     fileEncoding = "UTF-8", check.names = FALSE)
buildings  <- read.csv("buildings.csv",  fileEncoding = "UTF-8", check.names = FALSE)
outputs    <- read.csv("outputs.csv",    fileEncoding = "UTF-8", check.names = FALSE)
profitRate <- read.csv("profitRate.csv", fileEncoding = "UTF-8", check.names = FALSE)
gdp        <- read.csv("gdp.csv",        fileEncoding = "UTF-8", check.names = FALSE)
expans     <- read.csv("expansion.csv",  fileEncoding = "UTF-8", check.names = FALSE)
cashPool   <- read.csv("cashPool.csv",   fileEncoding = "UTF-8", check.names = FALSE)

show_plot <- function(expr, prompt = "按 Enter 继续...") {
  if (dev.cur() != 1) plot.new()
  expr
  invisible(readline(prompt = prompt))
}

cat("7 组图表，每次显示后按 Enter。\n")

show_plot({
  par(mfrow = c(4, 3), mar = c(3, 3, 2, 1))
  for (i in 2:ncol(prices)) plot(prices$Step, prices[[i]], type = "l", col = i,
                                 main = colnames(prices)[i], xlab = "Step", ylab = "价格")
  title("价格", outer = TRUE, line = -1)
})

show_plot({
  par(mfrow = c(4, 3), mar = c(3, 3, 2, 1))
  for (i in 2:ncol(buildings)) plot(buildings$Step, buildings[[i]], type = "l", col = i,
                                    main = colnames(buildings)[i], xlab = "Step", ylab = "数量")
  title("建筑", outer = TRUE)
})

show_plot({
  par(mfrow = c(4, 3), mar = c(3, 3, 2, 1))
  for (i in 2:ncol(outputs)) plot(outputs$Step, outputs[[i]], type = "l", col = i,
                                  main = gsub("产量$", "", colnames(outputs)[i]), xlab = "Step", ylab = "产量")
  title("产量", outer = TRUE)
})

show_plot({
  par(mfrow = c(4, 3), mar = c(3, 3, 2, 1))
  for (i in 2:ncol(profitRate)) plot(profitRate$Step, profitRate[[i]], type = "l", col = i,
                                     main = gsub("利润率$", "", colnames(profitRate)[i]), xlab = "Step", ylab = "利润率")
  title("利润率", outer = TRUE)
})

show_plot({
  par(mfrow = c(1, 1), mar = c(4, 4, 2, 1))
  plot(gdp$Step, gdp$GDP, type = "l", col = "darkblue", lwd = 2,
       main = "GDP", xlab = "Step", ylab = "GDP")
  grid(col = "gray80")
})

show_plot({
  par(mfrow = c(4, 3), mar = c(3, 3, 2, 1))
  for (i in 2:ncol(expans)) plot(expans$Step, expans[[i]], type = "h", col = i,
                                 main = colnames(expans)[i], xlab = "Step", ylab = "完工")
  title("完工建筑", outer = TRUE)
})

show_plot({
  par(mfrow = c(4, 3), mar = c(3, 3, 2, 1))
  for (i in 2:ncol(cashPool)) plot(cashPool$Step, cashPool[[i]], type = "l", col = i,
                                   main = colnames(cashPool)[i], xlab = "Step", ylab = "现金池")
  title("现金池", outer = TRUE)
})

cat("✅ 完成。\n")